/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#include "vbdev_ocf_cache.h"
#include "ctx.h"
#include "utils.h"

int
vbdev_ocf_cache_create(ocf_cache_t *out, const char *cache_name, const char *cache_mode,
		       const uint8_t cache_line_size, bool no_load)
{
	ocf_cache_t cache;
	struct ocf_mngt_cache_config *cache_cfg;
	struct vbdev_ocf_cache *cache_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': allocating structs and starting cache\n",
		      cache_name);

	cache_ctx = calloc(1, sizeof(struct vbdev_ocf_cache));
	if (!cache_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache context\n",
			    cache_name);
		return -ENOMEM;
	}

	cache_ctx->no_load = no_load;

	cache_cfg = &cache_ctx->cache_cfg;
	ocf_mngt_cache_config_set_default(cache_cfg);

	strncpy(cache_cfg->name, cache_name, OCF_CACHE_NAME_SIZE);

	if (cache_mode) {
		cache_cfg->cache_mode = vbdev_ocf_cachemode_get_by_name(cache_mode);
	}
	if (cache_line_size) {
		cache_cfg->cache_line_size = cache_line_size * KiB;
	}
	cache_cfg->locked = true;

	if ((rc = ocf_mngt_cache_start(vbdev_ocf_ctx, &cache, cache_cfg, cache_ctx))) {
		SPDK_ERRLOG("OCF cache '%s': failed to start OCF cache\n", cache_name);
		free(cache_ctx);
		return rc;
	}

	*out = cache;

	return rc;
}

void
vbdev_ocf_cache_destroy(ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': deallocating structs\n",
		      ocf_cache_get_name(cache));

	free(cache_ctx);
}

static void
_cache_hotremove_detach_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing hot removal\n",
		      ocf_cache_get_name(cache));

	// how to detach base despite the error (detached core) and not cause use-after-free on module fini?
	//vbdev_ocf_cache_base_detach(cache);

	ocf_mngt_cache_unlock(cache);

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to detach OCF cache device (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		return;
	}

	SPDK_NOTICELOG("OCF cache '%s': device detached\n", ocf_cache_get_name(cache));

	vbdev_ocf_cache_base_detach(cache);
	/* Increment queue refcount to prevent destroying management queue after cache device detach. */
	ocf_queue_get(((struct vbdev_ocf_cache *)ocf_cache_get_priv(cache))->cache_mngt_q);
}

static void
_cache_hotremove_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': detaching OCF cache device\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	}

	ocf_mngt_cache_detach(cache, _cache_hotremove_detach_cb, NULL);
}

static void
vbdev_ocf_cache_hotremove(struct spdk_bdev *bdev, void *event_ctx)
{
	ocf_cache_t cache = event_ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating hot removal of base bdev '%s'\n",
		      ocf_cache_get_name(cache), spdk_bdev_get_name(bdev));

	assert(bdev == ((struct vbdev_ocf_cache *)ocf_cache_get_priv(cache))->base.bdev);
	assert(ocf_cache_is_device_attached(cache));

	ocf_mngt_cache_lock(cache, _cache_hotremove_lock_cb, NULL);
}

static void
_vbdev_ocf_cache_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev, void *event_ctx)
{
	ocf_cache_t cache = event_ctx;

	switch (type) {
	case SPDK_BDEV_EVENT_REMOVE:
		vbdev_ocf_cache_hotremove(bdev, event_ctx);
		break;
	default:
		SPDK_NOTICELOG("OCF cache '%s': unsupported bdev event type: %d\n",
			       ocf_cache_get_name(cache), type);
	}
}

int
vbdev_ocf_cache_base_attach(ocf_cache_t cache, const char *base_name)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	struct vbdev_ocf_base *base = &cache_ctx->base;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': attaching base bdev '%s'\n",
		      ocf_cache_get_name(cache), base_name);

	strncpy(base->name, base_name, OCF_CACHE_NAME_SIZE);

	if ((rc = spdk_bdev_open_ext(base_name, true, _vbdev_ocf_cache_event_cb, cache, &base->desc))) {
		return rc;
	}

	if ((rc = spdk_bdev_module_claim_bdev_desc(base->desc, SPDK_BDEV_CLAIM_READ_MANY_WRITE_ONE,
						   NULL, &ocf_if))) {
		SPDK_ERRLOG("OCF cache '%s': failed to claim base bdev '%s'\n",
			    ocf_cache_get_name(cache), base_name);
		spdk_bdev_close(base->desc);
		return rc;
	}

	base->mngt_ch = spdk_bdev_get_io_channel(base->desc);
	if (!base->mngt_ch) {
		SPDK_ERRLOG("OCF cache '%s': failed to get IO channel for base bdev '%s'\n",
			    ocf_cache_get_name(cache), base_name);
		spdk_bdev_close(base->desc);
		return -ENOMEM;
	}

	base->bdev = spdk_bdev_desc_get_bdev(base->desc);
	base->thread = spdk_get_thread();
	base->is_cache = true;
	base->attached = true;

	// why not ? what is it then ?!?
	//assert(__bdev_to_io_dev(base->bdev) == base->bdev->ctxt);

	return rc;
}

void
vbdev_ocf_cache_base_detach(ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	struct vbdev_ocf_base *base = &cache_ctx->base;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': detaching base bdev '%s'\n",
		      ocf_cache_get_name(cache), spdk_bdev_get_name(base->bdev));

	vbdev_ocf_base_detach(base);
}

int
vbdev_ocf_cache_config_volume_create(ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	struct ocf_mngt_cache_attach_config *cache_att_cfg = &cache_ctx->cache_att_cfg;
	struct ocf_volume_uuid volume_uuid;
	int rc = 0;

	ocf_mngt_cache_attach_config_set_default(cache_att_cfg);
	cache_att_cfg->cache_line_size = cache_ctx->cache_cfg.cache_line_size;
	cache_att_cfg->open_cores = false;
	cache_att_cfg->discard_on_start = false; // needed ?
	cache_att_cfg->device.perform_test = false; // needed ?
	// for ocf_volume_open() in ocf_mngt_cache_attach/load()
	cache_att_cfg->device.volume_params = &cache_ctx->base;
	cache_att_cfg->force = cache_ctx->no_load;

	if ((rc = ocf_uuid_set_str(&volume_uuid, (char *)ocf_cache_get_name(cache)))) {
		SPDK_ERRLOG("OCF cache '%s': failed to set OCF volume uuid\n",
			    ocf_cache_get_name(cache));
		return rc;
	}

	if ((rc = ocf_ctx_volume_create(vbdev_ocf_ctx, &cache_att_cfg->device.volume,
					&volume_uuid, SPDK_OBJECT))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create OCF volume\n",
			    ocf_cache_get_name(cache));
		return rc;
	}

	return rc;
}

void
vbdev_ocf_cache_config_volume_destroy(ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	ocf_volume_destroy(cache_ctx->cache_att_cfg.device.volume);
}

static void
_volume_attach_metadata_probe_cb(void *priv, int error, struct ocf_metadata_probe_status *status)
{
	struct vbdev_ocf_mngt_ctx *ctx = priv;
	ocf_cache_t cache = ctx->cache;
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	ocf_volume_close(cache_ctx->cache_att_cfg.device.volume);

	if (error && error != -OCF_ERR_NO_METADATA) {
		SPDK_ERRLOG("OCF cache '%s': failed to probe metadata\n", ocf_cache_get_name(cache));
		ctx->u.att_cb_fn(cache, ctx, error);
	}

	if (error == -OCF_ERR_NO_METADATA) {
		SPDK_NOTICELOG("OCF cache '%s': metadata not found - starting new cache instance\n",
			       ocf_cache_get_name(cache));
		ocf_mngt_cache_attach(cache, &cache_ctx->cache_att_cfg, ctx->u.att_cb_fn, ctx);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': metadata found - loading previous cache instance\n",
			       ocf_cache_get_name(cache));
		SPDK_NOTICELOG("(start cache with 'no-load' flag to create new cache instead of loading config from metadata)\n");
		// check status for cache_name/mode/line_size/dirty ?
		ocf_mngt_cache_load(cache, &cache_ctx->cache_att_cfg, ctx->u.att_cb_fn, ctx);
	}
}

int
vbdev_ocf_cache_volume_attach(ocf_cache_t cache, struct vbdev_ocf_mngt_ctx *ctx)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	int rc;

	if (cache_ctx->no_load) {
		SPDK_NOTICELOG("'no-load' flag specified - starting new cache without looking for metadata\n");
		ocf_mngt_cache_attach(cache, &cache_ctx->cache_att_cfg, ctx->u.att_cb_fn, ctx);
		return 0;
	}

	if ((rc = ocf_volume_open(cache_ctx->cache_att_cfg.device.volume, &cache_ctx->base))) {
		SPDK_ERRLOG("OCF cache '%s': failed to open volume\n", ocf_cache_get_name(cache));
		return rc;
	}

	ocf_metadata_probe(vbdev_ocf_ctx, cache_ctx->cache_att_cfg.device.volume,
			   _volume_attach_metadata_probe_cb, ctx);

	return 0;
}

static void
_cache_mngt_queue_stop(void *ctx)
{
	struct vbdev_ocf_cache_mngt_queue_ctx *mngt_q_ctx = ctx;

	spdk_poller_unregister(&mngt_q_ctx->poller);
	free(mngt_q_ctx);
}

static void
vbdev_ocf_cache_mngt_queue_stop(ocf_queue_t queue)
{
	struct vbdev_ocf_cache_mngt_queue_ctx *mngt_q_ctx = ocf_queue_get_priv(queue);
	
	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': destroying OCF management queue\n",
		      ocf_cache_get_name(mngt_q_ctx->cache));

	if (mngt_q_ctx->thread && mngt_q_ctx->thread != spdk_get_thread()) {
		spdk_thread_send_msg(mngt_q_ctx->thread, _cache_mngt_queue_stop, mngt_q_ctx);
	} else {
		_cache_mngt_queue_stop(mngt_q_ctx);
	}
}

static void
vbdev_ocf_cache_mngt_queue_kick(ocf_queue_t queue)
{
}

const struct ocf_queue_ops cache_mngt_queue_ops = {
	.kick_sync = NULL,
	.kick = vbdev_ocf_cache_mngt_queue_kick,
	.stop = vbdev_ocf_cache_mngt_queue_stop,
};

int
vbdev_ocf_cache_mngt_queue_create(ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	struct vbdev_ocf_cache_mngt_queue_ctx *mngt_q_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': creating OCF management queue\n",
		      ocf_cache_get_name(cache));

	mngt_q_ctx = calloc(1, sizeof(struct vbdev_ocf_cache_mngt_queue_ctx));
	if (!mngt_q_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for management queue context\n",
			    ocf_cache_get_name(cache));
		return -ENOMEM;
	}

	if ((rc = ocf_queue_create_mngt(cache, &cache_ctx->cache_mngt_q, &cache_mngt_queue_ops))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create OCF management queue\n",
			    ocf_cache_get_name(cache));
		free(mngt_q_ctx);
		return rc;
	}
	ocf_queue_set_priv(cache_ctx->cache_mngt_q, mngt_q_ctx);

	mngt_q_ctx->poller = SPDK_POLLER_REGISTER(vbdev_ocf_queue_poller, cache_ctx->cache_mngt_q, 1000);
	if (!mngt_q_ctx->poller) {
		SPDK_ERRLOG("OCF cache '%s': failed to create management queue poller\n",
			    ocf_cache_get_name(cache));
		ocf_queue_put(cache_ctx->cache_mngt_q);
		return -ENOMEM;
	}

	mngt_q_ctx->cache = cache; // keep? (only for DEBUGLOG)
	mngt_q_ctx->thread = spdk_get_thread();

	return rc;
}

bool
vbdev_ocf_cache_is_base_attached(ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	return cache_ctx->base.attached;
}

//bool
//vbdev_ocf_any_cache_started(void)
//{
//	/* OCF context is created with refcount set to 1 and any started cache
//	 * will increment it further. So, if context refcount equals 1, it means
//	 * that it's just created without any started caches. */
//	//return (ocf_ctx_get_refcnt(vbdev_ocf_ctx) > 1) ? true : false;
//
//	int refcnt;
//
//	refcnt = ocf_ctx_get_refcnt(vbdev_ocf_ctx);
//	printf("*** CTX refcnt: %d\n", refcnt);
//	return (refcnt > 1) ? true : false;
//}
