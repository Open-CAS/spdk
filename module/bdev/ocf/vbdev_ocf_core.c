/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#include "spdk/string.h"

#include "vbdev_ocf_core.h"
#include "vbdev_ocf_cache.h"
#include "ctx.h"

struct vbdev_ocf_core_waitlist_head g_vbdev_ocf_core_waitlist =
		STAILQ_HEAD_INITIALIZER(g_vbdev_ocf_core_waitlist);

void
vbdev_ocf_core_waitlist_add(struct vbdev_ocf_core *core_ctx)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': adding to wait list\n",
		      vbdev_ocf_core_get_name(core_ctx));

	STAILQ_INSERT_TAIL(&g_vbdev_ocf_core_waitlist, core_ctx, waitlist_entry);
}

void
vbdev_ocf_core_waitlist_remove(struct vbdev_ocf_core *core_ctx)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': removing from wait list\n",
		      vbdev_ocf_core_get_name(core_ctx));

	STAILQ_REMOVE(&g_vbdev_ocf_core_waitlist, core_ctx, vbdev_ocf_core, waitlist_entry);
}

struct vbdev_ocf_core *
vbdev_ocf_core_waitlist_get_by_name(const char *core_name)
{
	struct vbdev_ocf_core *core_ctx;

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		if (!strcmp(core_name, vbdev_ocf_core_get_name(core_ctx))) {
			return core_ctx;
		}
	}

	return NULL;
}

char *
vbdev_ocf_core_get_name(struct vbdev_ocf_core *core_ctx)
{
	return core_ctx->core_cfg.name;
}

bool
vbdev_ocf_core_is_base_attached(struct vbdev_ocf_core *core_ctx)
{
	return core_ctx->base.attached;
}

static int
_core_is_loaded_cache_visitor(ocf_cache_t cache, void *cb_arg)
{
	const char *core_name = cb_arg;
	ocf_core_t core;
	int rc;

	rc = ocf_core_get_by_name(cache, core_name, OCF_CORE_NAME_SIZE, &core);
	if (!rc && !ocf_core_get_priv(core)) {
		/* Core context is assigned only after manual core add (either right
		 * away if all devices are present, after corresponding cache start,
		 * or base bdev appearance).
		 * If there is no context, it means that this core was just added from
		 * metadata during cache load and that's what we're looking for here. */

		return -EEXIST;
	} else if (rc && rc != -OCF_ERR_CORE_NOT_EXIST) {
		SPDK_ERRLOG("OCF: failed to get core: %s\n", spdk_strerror(-rc));
	}

	return 0;
}

bool
vbdev_ocf_core_is_loaded(const char *core_name)
{
	int rc;

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _core_is_loaded_cache_visitor, (char *)core_name);
	if (rc == -EEXIST) {
		return true;
	} else if (rc) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
	}

	return false;
}

int
vbdev_ocf_core_create(struct vbdev_ocf_core **out, const char *core_name, const char *cache_name)
{
	struct vbdev_ocf_core *core_ctx;
	struct ocf_mngt_core_config *core_cfg;
	int rc = 0;

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

	if ((rc = ocf_uuid_set_str(&core_cfg->uuid, core_cfg->name))) {
		SPDK_ERRLOG("OCF core '%s': failed to set OCF volume uuid\n", core_name);
		free(core_ctx);
		return rc;
	}

	*out = core_ctx;

	return rc;
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
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating hot removal of base bdev '%s'\n",
		      vbdev_ocf_core_get_name(core_ctx), spdk_bdev_get_name(bdev));

	assert(bdev == core_ctx->base.bdev);
	assert(vbdev_ocf_core_is_base_attached(core_ctx));

	if (vbdev_ocf_core_waitlist_get_by_name(vbdev_ocf_core_get_name(core_ctx))) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': hot removing from wait list\n",
			      vbdev_ocf_core_get_name(core_ctx));

		vbdev_ocf_core_base_detach(core_ctx);
		return;
	}

	if ((rc = vbdev_ocf_core_unregister(core_ctx, NULL, NULL))) {
		SPDK_ERRLOG("OCF core '%s': failed to start unregistering OCF vbdev during core hot removal: %s\n",
			    vbdev_ocf_core_get_name(core_ctx), spdk_strerror(-rc));
		/* Base bdev is already removed so detach it despite the error. */
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

	strncpy(base->name, base_name, OCF_CORE_NAME_SIZE);

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
	core_cfg->volume_type = SPDK_OBJECT;
	// for ocf_volume_open() in ocf_mngt_cache_add_core()
	core_cfg->volume_params = base;

	return rc;
}

void
vbdev_ocf_core_base_detach(struct vbdev_ocf_core *core_ctx)
{
	struct vbdev_ocf_base *base = &core_ctx->base;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': detaching base bdev '%s'\n",
		      vbdev_ocf_core_get_name(core_ctx), spdk_bdev_get_name(base->bdev));

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

	/* Core channel may not exist only on error path. */
	if (likely(ch_ctx->core_ch)) {
		spdk_put_io_channel(ch_ctx->core_ch);
	}

	free(ch_ctx);
}

static void
vbdev_ocf_core_io_queue_stop(ocf_queue_t queue)
{
	struct vbdev_ocf_core_io_channel_ctx *ch_ctx = ocf_queue_get_priv(queue);
	struct spdk_bdev *ocf_vbdev = &((struct vbdev_ocf_core *)ocf_core_get_priv(ch_ctx->core))->ocf_vbdev;
	int rc;
	
	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': deallocating external IO channel context\n",
		      spdk_bdev_get_name(ocf_vbdev));

	if (ch_ctx->thread && ch_ctx->thread != spdk_get_thread()) {
		if ((rc = spdk_thread_send_msg(ch_ctx->thread, _core_io_queue_stop, ch_ctx))) {
			SPDK_ERRLOG("OCF vbdev '%s': failed to send message to thread (name: %s, id: %ld): %s\n",
				    spdk_bdev_get_name(ocf_vbdev),
				    spdk_thread_get_name(ch_ctx->thread),
				    spdk_thread_get_id(ch_ctx->thread),
				    spdk_strerror(-rc));
			assert(false);
		}
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
	ch_ctx->core = core; // keep? (only for DEBUGLOG)
	ch_ctx->thread = spdk_get_thread();

	if ((rc = ocf_queue_create(cache, &ch_ctx->queue, &core_io_queue_ops))) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to create OCF queue\n", vbdev_name);
		free(ch_ctx);
		return rc;
	}
	ocf_queue_set_priv(ch_ctx->queue, ch_ctx);
	/* Save queue pointer in buffer provided by the IO channel callback.
	 * Only this will be needed in channel destroy callback to decrement
	 * the refcount. The rest is freed in queue stop callback. */
	ch_destroy_ctx->queue = ch_ctx->queue;

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
		/* Do not spdk_put_io_channel() here as it
		 * will be done during stop of OCF queue. */
		ocf_queue_put(ch_ctx->queue);
		return -ENOMEM;
	}

	ch_ctx->poller = SPDK_POLLER_REGISTER(vbdev_ocf_queue_poller, ch_ctx->queue, 0);
	if (!ch_ctx->poller) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to create IO queue poller\n", vbdev_name);
		/* Do not spdk_put_io_channel() here as it
		 * will be done during stop of OCF queue. */
		ocf_queue_put(ch_ctx->queue);
		return -ENOMEM;
	}

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
	// cache_line_size align ?
	ocf_vbdev->required_alignment = base->bdev->required_alignment;
	ocf_vbdev->optimal_io_boundary = base->bdev->optimal_io_boundary;
	// generate UUID based on namespace UUID + base bdev UUID (take from old module?)
	ocf_vbdev->fn_table = &vbdev_ocf_fn_table;
	ocf_vbdev->module = &ocf_if;

	spdk_io_device_register(core, _vbdev_ocf_ch_create_cb, _vbdev_ocf_ch_destroy_cb,
				sizeof(struct vbdev_ocf_core_io_channel_ctx), ocf_core_get_name(core));
	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': io_device created at %p\n",
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

static void
_core_add_from_waitlist_err_cb(void *cb_arg, int error)
{
	ocf_cache_t cache = cb_arg;

	if (error) {
		SPDK_ERRLOG("OCF core: failed to remove OCF core device (OCF error: %d)\n", error);
	}

	ocf_mngt_cache_unlock(cache);
}

static void
_core_add_from_waitlist_add_cb(ocf_cache_t cache, ocf_core_t core, void *cb_arg, int error)
{
	struct vbdev_ocf_core *core_ctx = cb_arg;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing add of OCF core\n",
		      vbdev_ocf_core_get_name(core_ctx));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to add core to OCF cache '%s' (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), ocf_cache_get_name(cache), error);
		ocf_mngt_cache_unlock(cache);
		return;
	}

	ocf_core_set_priv(core, core_ctx);

	if ((rc = vbdev_ocf_core_register(core))) {
		SPDK_ERRLOG("OCF core '%s': failed to register vbdev: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-rc));
		ocf_mngt_cache_remove_core(core, _core_add_from_waitlist_err_cb, cache);
		return;
	}

	SPDK_NOTICELOG("OCF core '%s': added to cache '%s'\n",
		       ocf_core_get_name(core), ocf_cache_get_name(cache));

	ocf_mngt_cache_unlock(cache);
	vbdev_ocf_core_waitlist_remove(core_ctx);
}

static void
_core_add_from_waitlist_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_core *core_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating add of OCF core\n",
		      vbdev_ocf_core_get_name(core_ctx));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), error);
		return;
	}

	ocf_mngt_cache_add_core(cache, &core_ctx->core_cfg, _core_add_from_waitlist_add_cb, core_ctx);
}

void
vbdev_ocf_core_add_from_waitlist(ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	struct vbdev_ocf_core *core_ctx;
	uint32_t cache_block_size = spdk_bdev_get_block_size(cache_ctx->base.bdev);
	uint32_t core_block_size;

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		if (strcmp(ocf_cache_get_name(cache), core_ctx->cache_name) ||
				!vbdev_ocf_core_is_base_attached(core_ctx)) {
			continue;
		}

		SPDK_NOTICELOG("OCF core '%s': adding from waitlist to cache '%s'\n",
			       vbdev_ocf_core_get_name(core_ctx), ocf_cache_get_name(cache));

		core_block_size = spdk_bdev_get_block_size(core_ctx->base.bdev);
		if (cache_block_size > core_block_size) {
			SPDK_ERRLOG("OCF core '%s': failed to add to cache '%s': cache block size (%d) is greater than core block size (%d)\n",
				    vbdev_ocf_core_get_name(core_ctx), ocf_cache_get_name(cache),
				    cache_block_size, core_block_size);
			continue;
		}

		core_ctx->core_cfg.try_add = vbdev_ocf_core_is_loaded(vbdev_ocf_core_get_name(core_ctx));

		ocf_mngt_cache_lock(cache, _core_add_from_waitlist_lock_cb, core_ctx);
	}
}

static void
_create_cache_ch_cb(struct spdk_io_channel_iter *i, int error)
{
	ocf_core_t core = spdk_io_channel_iter_get_io_device(i);

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to create cache IO channels: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-error));
		return;
	}

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': all cache IO channels created\n",
		      ocf_core_get_name(core));
}

static void
_create_cache_ch_fn(struct spdk_io_channel_iter *i)
{
	ocf_cache_t cache = spdk_io_channel_iter_get_ctx(i);
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	struct vbdev_ocf_core_io_channel_ctx *ch_ctx, *queue_ch_ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s' on channel %p: creating cache IO channel\n",
		      ocf_core_get_name(spdk_io_channel_iter_get_io_device(i)),
		      spdk_io_channel_iter_get_channel(i));

	/* The actual IO channel context is saved in queue priv.
	 * The one saved in channel context buffer is used only
	 * to properly destroy the channel.
	 * (See the comment in _vbdev_ocf_ch_create_cb() in vbdev_ocf_core.c
	 * for more details.) */
	ch_ctx = spdk_io_channel_get_ctx(spdk_io_channel_iter_get_channel(i));
	queue_ch_ctx = ocf_queue_get_priv(ch_ctx->queue);

	/* Cache device IO channel should not exist at this point.
	 * It should be cleaned after cache device detach (either
	 * manual or hot removed) or not initialized after starting
	 * cache without device present. */
	assert(!queue_ch_ctx->cache_ch);

	queue_ch_ctx->cache_ch = spdk_bdev_get_io_channel(cache_ctx->base.desc);

	spdk_for_each_channel_continue(i, 0);
}

static int
_create_cache_ch_core_visitor(ocf_core_t core, void *ctx)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': creating all cache IO channels\n",
		      ocf_core_get_name(core));

	spdk_for_each_channel(core, _create_cache_ch_fn, ctx, _create_cache_ch_cb);

	return 0;
}

int
vbdev_ocf_core_create_cache_channel(ocf_cache_t cache)
{
	return ocf_core_visit(cache, _create_cache_ch_core_visitor, cache, true);
}

static void
_destroy_cache_ch_cb(struct spdk_io_channel_iter *i, int error)
{
	ocf_core_t core = spdk_io_channel_iter_get_io_device(i);

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to destroy cache IO channels: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-error));
		return;
	}

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': all cache IO channels destroyed\n",
		      ocf_core_get_name(core));
}

static void
_destroy_cache_ch_fn(struct spdk_io_channel_iter *i)
{
	struct vbdev_ocf_core_io_channel_ctx *ch_ctx, *queue_ch_ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s' on channel %p: destroying cache IO channel\n",
		      ocf_core_get_name(spdk_io_channel_iter_get_io_device(i)),
		      spdk_io_channel_iter_get_channel(i));

	/* The actual IO channel context is saved in queue priv.
	 * The one saved in channel context buffer is used only
	 * to properly destroy the channel.
	 * (See the comment in _vbdev_ocf_ch_create_cb() in vbdev_ocf_core.c
	 * for more details.) */
	ch_ctx = spdk_io_channel_get_ctx(spdk_io_channel_iter_get_channel(i));
	queue_ch_ctx = ocf_queue_get_priv(ch_ctx->queue);

	assert(queue_ch_ctx->cache_ch);

	spdk_put_io_channel(queue_ch_ctx->cache_ch);
	queue_ch_ctx->cache_ch = NULL;

	spdk_for_each_channel_continue(i, 0);
}

static int
_destroy_cache_ch_core_visitor(ocf_core_t core, void *ctx)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': destroying all cache IO channels\n",
		      ocf_core_get_name(core));

	spdk_for_each_channel(core, _destroy_cache_ch_fn, ctx, _destroy_cache_ch_cb);

	return 0;
}

int
vbdev_ocf_core_destroy_cache_channel(ocf_cache_t cache)
{
	return ocf_core_visit(cache, _destroy_cache_ch_core_visitor, NULL, true);
}
