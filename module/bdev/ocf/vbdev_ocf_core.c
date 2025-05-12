/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#include "spdk/string.h" // rm ?

#include "vbdev_ocf_core.h"
#include "vbdev_ocf_cache.h"
#include "ctx.h"

struct vbdev_ocf_core_waitlist_head g_vbdev_ocf_core_waitlist =
		STAILQ_HEAD_INITIALIZER(g_vbdev_ocf_core_waitlist);

int
vbdev_ocf_core_create(struct vbdev_ocf_core **out, const char *core_name, const char *cache_name)
{
	struct vbdev_ocf_core *core_ctx;
	struct ocf_mngt_core_config *core_cfg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': allocating structs\n",
		      core_name);

	core_ctx = calloc(1, sizeof(struct vbdev_ocf_core));
	if (!core_ctx) {
		SPDK_ERRLOG("OCF core '%s': failed to allocate memory for core context\n",
			    core_name);
		return -ENOMEM;
	}

	core_cfg = &core_ctx->core_cfg;
	ocf_mngt_core_config_set_default(core_cfg);
	strncpy(core_cfg->name, core_name, OCF_CORE_NAME_SIZE);
	strncpy(core_ctx->cache_name, cache_name, OCF_CACHE_NAME_SIZE);

	*out = core_ctx;

	return 0;
}

void
vbdev_ocf_core_destroy(struct vbdev_ocf_core *core_ctx)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': deallocating structs\n",
		      vbdev_ocf_core_get_name(core_ctx));

	free(core_ctx);
}

static void
vbdev_ocf_core_hotremove(struct spdk_bdev *bdev, void *event_ctx)
{
	struct vbdev_ocf_core *core_ctx = event_ctx;
	struct vbdev_ocf_core *core_ctx_waitlist;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating hot removal of base bdev '%s'\n",
		      vbdev_ocf_core_get_name(core_ctx), spdk_bdev_get_name(bdev));

	assert(bdev == core_ctx->base.bdev);

	vbdev_ocf_foreach_core_in_waitlist(core_ctx_waitlist) {
		if (core_ctx == core_ctx_waitlist) {
			SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': hot removing from wait list\n",
				      vbdev_ocf_core_get_name(core_ctx));
			// always true ?
			if (vbdev_ocf_core_is_base_attached(core_ctx)) {
				vbdev_ocf_core_base_detach(core_ctx);
				return;
			}
		}
	}

	if ((rc = vbdev_ocf_core_unregister(core_ctx, NULL, NULL))) {
		SPDK_ERRLOG("OCF core '%s': failed to start unregistering OCF vbdev during core hot removal: %s\n",
			    vbdev_ocf_core_get_name(core_ctx), spdk_strerror(-rc));
		// detach base despite the error ?
		vbdev_ocf_core_base_detach(core_ctx);
	}
}

static void
_vbdev_ocf_core_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev, void *event_ctx)
{
	struct vbdev_ocf_core *core_ctx = event_ctx;

	switch (type) {
	case SPDK_BDEV_EVENT_REMOVE:
		vbdev_ocf_core_hotremove(bdev, event_ctx);
		break;
	default:
		SPDK_NOTICELOG("OCF core '%s': unsupported bdev event type: %d\n",
			       vbdev_ocf_core_get_name(core_ctx), type);
	}
}

int
vbdev_ocf_core_base_attach(struct vbdev_ocf_core *core_ctx, const char *base_name)
{
	struct vbdev_ocf_base *base = &core_ctx->base;
	struct ocf_mngt_core_config *core_cfg = &core_ctx->core_cfg;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': attaching base bdev '%s'\n",
		      vbdev_ocf_core_get_name(core_ctx), base_name);

	if ((rc = spdk_bdev_open_ext(base_name, true, _vbdev_ocf_core_event_cb, core_ctx, &base->desc))) {
		return rc;
	}

	if ((rc = spdk_bdev_module_claim_bdev_desc(base->desc, SPDK_BDEV_CLAIM_READ_MANY_WRITE_ONE,
						   NULL, &ocf_if))) {
		SPDK_ERRLOG("OCF core '%s': failed to claim base bdev '%s'\n",
			    vbdev_ocf_core_get_name(core_ctx), base_name);
		spdk_bdev_close(base->desc);
		return rc;
	}

	base->mngt_ch = spdk_bdev_get_io_channel(base->desc);
	if (!base->mngt_ch) {
		SPDK_ERRLOG("OCF core '%s': failed to get IO channel for base bdev '%s'\n",
			    vbdev_ocf_core_get_name(core_ctx), base_name);
		spdk_bdev_close(base->desc);
		return -ENOMEM;
	}

	base->bdev = spdk_bdev_desc_get_bdev(base->desc);
	base->thread = spdk_get_thread();
	base->is_cache = false;
	base->attached = true;

	// alloc uuid ?
	if ((rc = ocf_uuid_set_str(&core_cfg->uuid, (char *)base_name))) {
		SPDK_ERRLOG("OCF core '%s': failed to set OCF volume uuid\n",
			    vbdev_ocf_core_get_name(core_ctx));
		spdk_put_io_channel(base->mngt_ch);
		spdk_bdev_close(base->desc);
		return rc;
	}

	core_cfg->volume_type = SPDK_OBJECT;
	// for ocf_volume_open() in ocf_mngt_cache_add_core()
	core_cfg->volume_params = base;

	// why not ? what is it then ?!?
	//assert(__bdev_to_io_dev(base->bdev) == base->bdev->ctxt);

	return rc;
}

void
vbdev_ocf_core_base_detach(struct vbdev_ocf_core *core_ctx)
{
	struct vbdev_ocf_base *base = &core_ctx->base;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': detaching base bdev '%s'\n",
		      vbdev_ocf_core_get_name(core_ctx), spdk_bdev_get_name(base->bdev));

	// dealloc uuid ?

	vbdev_ocf_base_detach(base);
}

static void
_core_io_queue_stop(void *ctx)
{
	struct vbdev_ocf_core_io_channel_ctx *ch_ctx = ctx;

	spdk_poller_unregister(&ch_ctx->poller);
	if (ch_ctx->cache_ch) {
		spdk_put_io_channel(ch_ctx->cache_ch);
	}
	spdk_put_io_channel(ch_ctx->core_ch);
	free(ch_ctx);
}

static void
vbdev_ocf_core_io_queue_stop(ocf_queue_t queue)
{
	struct vbdev_ocf_core_io_channel_ctx *ch_ctx = ocf_queue_get_priv(queue);
	
	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': deallocating external IO channel context\n",
		      spdk_bdev_get_name(&((struct vbdev_ocf_core *)ocf_core_get_priv(ch_ctx->core))->ocf_vbdev));

	if (ch_ctx->thread && ch_ctx->thread != spdk_get_thread()) {
		spdk_thread_send_msg(ch_ctx->thread, _core_io_queue_stop, ch_ctx);
	} else {
		_core_io_queue_stop(ch_ctx);
	}
}

static void
vbdev_ocf_core_io_queue_kick(ocf_queue_t queue)
{
}

const struct ocf_queue_ops core_io_queue_ops = {
	.kick_sync = NULL,
	.kick = vbdev_ocf_core_io_queue_kick,
	.stop = vbdev_ocf_core_io_queue_stop,
};

static int
_vbdev_ocf_ch_create_cb(void *io_device, void *ctx_buf)
{
	ocf_core_t core = io_device;
	ocf_cache_t cache = ocf_core_get_cache(core);
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	struct vbdev_ocf_base *core_base = &core_ctx->base;
	struct vbdev_ocf_base *cache_base = &cache_ctx->base;
	struct vbdev_ocf_core_io_channel_ctx *ch_destroy_ctx = ctx_buf;
	struct vbdev_ocf_core_io_channel_ctx *ch_ctx;
	const char *vbdev_name = spdk_bdev_get_name(&core_ctx->ocf_vbdev);
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': creating IO channel and allocating external context\n",
		      vbdev_name);

	/* Do not use provided buffer for IO channel context, as it will be freed
	 * when this channel is destroyed. Instead allocate our own and keep it
	 * in queue priv. It will be needed later, after the channel was closed,
	 * as it may be referenced by backfill. */
	ch_ctx = calloc(1, sizeof(struct vbdev_ocf_core_io_channel_ctx));
	if (!ch_ctx) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to allocate memory for IO channel context\n",
			    vbdev_name);
		return -ENOMEM;
	}

	if ((rc = ocf_queue_create(cache, &ch_ctx->queue, &core_io_queue_ops))) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to create OCF queue\n", vbdev_name);
		free(ch_ctx);
		return rc;
	}
	ocf_queue_set_priv(ch_ctx->queue, ch_ctx);

	if (!ocf_cache_is_detached(cache)) {
		ch_ctx->cache_ch = spdk_bdev_get_io_channel(cache_base->desc);
		if (!ch_ctx->cache_ch) {
			SPDK_ERRLOG("OCF vbdev '%s': failed to create IO channel for base bdev '%s'\n",
				    vbdev_name, spdk_bdev_get_name(cache_base->bdev));
			ocf_queue_put(ch_ctx->queue);
			return -ENOMEM;
		}
	}

	ch_ctx->core_ch = spdk_bdev_get_io_channel(core_base->desc);
	if (!ch_ctx->core_ch) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to create IO channel for base bdev '%s'\n",
			    vbdev_name, spdk_bdev_get_name(core_base->bdev));
		spdk_put_io_channel(ch_ctx->cache_ch);
		ocf_queue_put(ch_ctx->queue);
		return -ENOMEM;
	}

	ch_ctx->poller = SPDK_POLLER_REGISTER(vbdev_ocf_queue_poller, ch_ctx->queue, 0);
	if (!ch_ctx->poller) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to create IO queue poller\n", vbdev_name);
		spdk_put_io_channel(ch_ctx->core_ch);
		spdk_put_io_channel(ch_ctx->cache_ch);
		ocf_queue_put(ch_ctx->queue);
		return -ENOMEM;
	}

	ch_ctx->core = core; // keep? (only for DEBUGLOG)
	ch_ctx->thread = spdk_get_thread();

	/* Save queue pointer in buffer provided by the IO channel callback.
	 * Only this will be needed in channel destroy callback to decrement
	 * the refcount. The rest is freed in queue stop callback. */
	ch_destroy_ctx->queue = ch_ctx->queue;

	return rc;
}

static void
_vbdev_ocf_ch_destroy_cb(void *io_device, void *ctx_buf)
{
	struct vbdev_ocf_core_io_channel_ctx *ch_destroy_ctx = ctx_buf;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': destroying IO channel\n",
		      spdk_bdev_get_name(&(((struct vbdev_ocf_core *)ocf_core_get_priv(
				      (ocf_core_t)io_device))->ocf_vbdev)));

	ocf_queue_put(ch_destroy_ctx->queue);
}

int
vbdev_ocf_core_register(ocf_core_t core)
{
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	struct vbdev_ocf_base *base = &core_ctx->base;
	struct spdk_bdev *ocf_vbdev = &core_ctx->ocf_vbdev;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': registering OCF vbdev in SPDK bdev layer\n",
		      ocf_core_get_name(core));

	ocf_vbdev->ctxt = core;
	ocf_vbdev->name = (char *)ocf_core_get_name(core);
	ocf_vbdev->product_name = "OCF_disk";
	ocf_vbdev->write_cache = base->bdev->write_cache;
	ocf_vbdev->blocklen = base->bdev->blocklen;
	ocf_vbdev->blockcnt = base->bdev->blockcnt;
	// ?
	//ocf_vbdev->required_alignment = base->bdev->required_alignment;
	//ocf_vbdev->optimal_io_boundary = base->bdev->optimal_io_boundary;
	// generate UUID based on namespace UUID + base bdev UUID (take from old module?)
	ocf_vbdev->fn_table = &vbdev_ocf_fn_table;
	ocf_vbdev->module = &ocf_if;

	spdk_io_device_register(core, _vbdev_ocf_ch_create_cb, _vbdev_ocf_ch_destroy_cb,
				sizeof(struct vbdev_ocf_core_io_channel_ctx), ocf_core_get_name(core));
	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': io_device created at 0x%p\n",
		      spdk_bdev_get_name(ocf_vbdev), core);

	if ((rc = spdk_bdev_register(ocf_vbdev))) { // needs to be called from SPDK app thread
		SPDK_ERRLOG("OCF vbdev '%s': failed to register SPDK bdev\n",
			    spdk_bdev_get_name(ocf_vbdev));
		spdk_io_device_unregister(core, NULL);
		return rc;
	}

	return rc;
}

int
vbdev_ocf_core_unregister(struct vbdev_ocf_core *core_ctx, spdk_bdev_unregister_cb cb_fn, void *cb_arg)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating unregister of OCF vbdev\n",
		      vbdev_ocf_core_get_name(core_ctx));

	return spdk_bdev_unregister_by_name(spdk_bdev_get_name(&core_ctx->ocf_vbdev),
					    &ocf_if, cb_fn, cb_arg);
}

char *
vbdev_ocf_core_get_name(struct vbdev_ocf_core *core_ctx)
{
	return core_ctx->core_cfg.name;
}

struct vbdev_ocf_core *
vbdev_ocf_core_waitlist_get_by_name(const char *core_name)
{
	struct vbdev_ocf_core *core_ctx;

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		// if (core_ctx && ...) ?
		if (!strcmp(core_name, vbdev_ocf_core_get_name(core_ctx))) {
			return core_ctx;
		}
	}

	return NULL;
}

bool
vbdev_ocf_core_is_base_attached(struct vbdev_ocf_core *core_ctx)
{
	return core_ctx->base.attached;
}
