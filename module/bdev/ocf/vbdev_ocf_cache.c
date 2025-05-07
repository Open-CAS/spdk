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
	struct ocf_mngt_cache_attach_config *cache_att_cfg;
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

	cache_cfg = &cache_ctx->cache_cfg;
	cache_att_cfg = &cache_ctx->cache_att_cfg;

	ocf_mngt_cache_config_set_default(cache_cfg);
	ocf_mngt_cache_attach_config_set_default(cache_att_cfg);

	strncpy(cache_cfg->name, cache_name, OCF_CACHE_NAME_SIZE);
	if (cache_mode) {
		cache_cfg->cache_mode = ocf_get_cache_mode(cache_mode);
	}
	if (cache_line_size) {
		cache_cfg->cache_line_size = cache_line_size * KiB;
		cache_att_cfg->cache_line_size = cache_line_size * KiB;
	}
	cache_cfg->locked = true;
	cache_att_cfg->open_cores = false;
	cache_att_cfg->discard_on_start = false; // needed ?
	cache_att_cfg->device.perform_test = false; // needed ?
	cache_att_cfg->force = no_load;

	if ((rc = ocf_mngt_cache_start(vbdev_ocf_ctx, &cache, cache_cfg, cache_ctx))) {
		SPDK_ERRLOG("OCF cache '%s': failed to start OCF cache\n", cache_name);
		free(cache_ctx);
		return rc;
	}

	// needed ?
	//if ((rc = ocf_mngt_cache_get(cache))) {
	//	SPDK_ERRLOG("OCF cache '%s': failed to increment cache ref count: %s\n",
	//		    cache_name, spdk_strerror(-rc));
	//	ocf_mngt_cache_stop(cache, NULL, NULL); // needs callback (_cache_start_err_cb ?)
	//	free(cache_ctx);
	//	return rc;
	//}

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

// vbdev_ocf_cache_detach() instead of this ?
static void
vbdev_ocf_cache_hotremove(struct spdk_bdev *bdev, void *event_ctx)
{
	ocf_cache_t cache = event_ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': hot removal of base bdev '%s'\n",
		      ocf_cache_get_name(cache), spdk_bdev_get_name(bdev));

	assert(bdev == ((struct vbdev_ocf_cache *)ocf_cache_get_priv(cache))->base.bdev);

	if (ocf_cache_is_device_attached(cache)) { // always true?
		// OCF cache detach
		// (to comply with SPDK hotremove support)
	}

	vbdev_ocf_cache_base_detach(cache); // in detach callback ?
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
	struct ocf_volume_uuid volume_uuid;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': attaching base bdev '%s'\n",
		      ocf_cache_get_name(cache), base_name);

	//strncpy(base->name, base_name, OCF_CACHE_NAME_SIZE);

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

	if ((rc = ocf_uuid_set_str(&volume_uuid, (char *)base_name))) {
		SPDK_ERRLOG("OCF cache '%s': failed to set OCF volume uuid\n",
			    ocf_cache_get_name(cache));
		spdk_put_io_channel(base->mngt_ch);
		spdk_bdev_close(base->desc);
		return rc;
	}

	if ((rc = ocf_ctx_volume_create(vbdev_ocf_ctx, &cache_ctx->cache_att_cfg.device.volume,
					&volume_uuid, SPDK_OBJECT))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create OCF volume\n", ocf_cache_get_name(cache));
		spdk_put_io_channel(base->mngt_ch);
		spdk_bdev_close(base->desc);
		return rc;
	}

	// for ocf_volume_open() in ocf_mngt_cache_attach/load()
	cache_ctx->cache_att_cfg.device.volume_params = base;

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


	//if (cache_ctx->cache_att_cfg.device.volume) {
	//	ocf_volume_destroy(cache_ctx->cache_att_cfg.device.volume);
	//	cache_ctx->cache_att_cfg.device.volume = NULL;
	//	//cache_ctx->cache_att_cfg.device.volume_params = NULL; // ?
	//}
	ocf_volume_destroy(cache_ctx->cache_att_cfg.device.volume);

	vbdev_ocf_base_detach(base);
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

	if ((rc = vbdev_ocf_queue_create_mngt(cache, &cache_ctx->cache_mngt_q, &cache_mngt_queue_ops))) {
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
		vbdev_ocf_queue_put(cache_ctx->cache_mngt_q);
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
