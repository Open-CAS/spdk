/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#include <ocf/ocf.h>

#include "spdk/bdev_module.h"
#include "spdk/string.h"

#include "vbdev_ocf.h"
#include "ctx.h"
#include "data.h"
#include "utils.h"
#include "volume.h"

/* This namespace UUID was generated using uuid_generate() method. */
#define BDEV_OCF_NAMESPACE_UUID "f92b7f49-f6c0-44c8-bd23-3205e8c3b6ad"

static int vbdev_ocf_module_init(void);
static void vbdev_ocf_module_fini_start(void);
static void vbdev_ocf_module_fini(void);
static int vbdev_ocf_module_get_ctx_size(void);
static void vbdev_ocf_module_examine_config(struct spdk_bdev *bdev);
static void vbdev_ocf_module_examine_disk(struct spdk_bdev *bdev);

struct spdk_bdev_module ocf_if = {
	.name = "OCF",
	.module_init = vbdev_ocf_module_init,
	.fini_start = vbdev_ocf_module_fini_start,
	.module_fini = vbdev_ocf_module_fini,
	.get_ctx_size = vbdev_ocf_module_get_ctx_size,
	.examine_config = vbdev_ocf_module_examine_config,
	.examine_disk = vbdev_ocf_module_examine_disk,
	.async_fini_start = true,
};

SPDK_BDEV_MODULE_REGISTER(ocf, &ocf_if)

static int vbdev_ocf_fn_destruct(void *ctx);
static void vbdev_ocf_fn_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io);
static bool vbdev_ocf_fn_io_type_supported(void *ctx, enum spdk_bdev_io_type);
static struct spdk_io_channel *vbdev_ocf_fn_get_io_channel(void *ctx);
static int vbdev_ocf_fn_dump_info_json(void *ctx, struct spdk_json_write_ctx *w);
static void vbdev_ocf_fn_write_config_json(struct spdk_bdev *bdev, struct spdk_json_write_ctx *w);

struct spdk_bdev_fn_table vbdev_ocf_fn_table = {
	.destruct = vbdev_ocf_fn_destruct,
	.submit_request = vbdev_ocf_fn_submit_request,
	.io_type_supported = vbdev_ocf_fn_io_type_supported,
	.get_io_channel = vbdev_ocf_fn_get_io_channel,
	.dump_info_json = vbdev_ocf_fn_dump_info_json,
	.write_config_json = vbdev_ocf_fn_write_config_json,
	.dump_device_stat_json = NULL, // todo ?
	.reset_device_stat = NULL, // todo ?
};

// rm
#define __bdev_to_io_dev(bdev)          (((char *)bdev) + 1)

static int
_bdev_exists_core_visit(ocf_core_t core, void *cb_arg)
{
	const char *name = cb_arg;
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);

	if (!core_ctx) {
		/* Skip this core. If there is no context, it means that this core
		 * was added from metadata during cache load and it's just an empty shell. */
		return 0;
	}

	if (!strcmp(name, ocf_core_get_name(core))) {
		return -EEXIST;
	}

	return 0;
}

static int
_bdev_exists_cache_visit(ocf_cache_t cache, void *cb_arg)
{
	char *name = cb_arg;

	if (!strcmp(name, ocf_cache_get_name(cache))) {
		return -EEXIST;
	}

	return ocf_core_visit(cache, _bdev_exists_core_visit, name, false);
}

static bool
vbdev_ocf_bdev_exists(const char *name)
{
	struct vbdev_ocf_core *core_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF: looking for '%s' in existing bdev names\n", name);

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		if (!strcmp(name, vbdev_ocf_core_get_name(core_ctx))) {
			return true;
		}
	}

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _bdev_exists_cache_visit, (char *)name);
	if (rc == -EEXIST) {
		return true;
	} else if (rc) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
	}

	if (spdk_bdev_get_by_name(name)) {
		return true;
	}

	return false;
}

static int
vbdev_ocf_module_init(void)
{
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF: starting module\n");

	if ((rc = vbdev_ocf_ctx_init())) {
		SPDK_ERRLOG("OCF: failed to initialize context: %d\n", rc);
		return rc;
	}

	if ((rc = vbdev_ocf_volume_init())) {
		vbdev_ocf_ctx_cleanup();
		SPDK_ERRLOG("OCF: failed to register volume: %d\n", rc);
		return rc;
	}

	return rc;
}

static void
_cache_stop_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_stop_ctx = cb_arg;
	ocf_queue_t cache_mngt_q = ((struct vbdev_ocf_cache *)ocf_cache_get_priv(cache))->cache_mngt_q;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing stop of OCF cache\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to stop OCF cache (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': stopped\n", ocf_cache_get_name(cache));
	}

	/* In module fini (no cache stop context) do the cleanup despite the error. */
	if (!error || !cache_stop_ctx) {
		if (vbdev_ocf_cache_is_base_attached(cache)) {
			vbdev_ocf_cache_base_detach(cache);
		} else {
			/* If device was not attached to cache, then
			 * cache stop won't put its management queue. */
			ocf_queue_put(cache_mngt_q);
		}
		vbdev_ocf_cache_destroy(cache);
	}

	ocf_mngt_cache_unlock(cache);

	if (cache_stop_ctx) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing stop\n",
			      ocf_cache_get_name(cache));

		cache_stop_ctx->rpc_cb_fn(NULL, cache_stop_ctx->rpc_cb_arg, error); // cache name
		free(cache_stop_ctx);
	} else if (!ocf_mngt_cache_get_count(vbdev_ocf_ctx)) {
		/* In module fini (no cache stop context) call spdk_bdev_module_fini_start_done()
		 * if there are no caches left to stop. */
		spdk_bdev_module_fini_start_done();
	}
}

static void
_cache_stop_flush_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to flush OCF cache (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	}

	ocf_mngt_cache_stop(cache, _cache_stop_cb, cb_arg);
}

static void
_cache_stop_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating stop of OCF cache\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	}

	// no need to manually flush ?
	if (ocf_mngt_cache_is_dirty(cache)) {
		ocf_mngt_cache_flush(cache, _cache_stop_flush_cb, cb_arg);
	} else {
		ocf_mngt_cache_stop(cache, _cache_stop_cb, cb_arg);
	}
}

static void
_cache_stop_core_unregister_cb(void *cb_arg, int error)
{
	ocf_core_t core = cb_arg;
	ocf_cache_t cache = ocf_core_get_cache(core);
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	struct vbdev_ocf_mngt_ctx *cache_stop_ctx = core_ctx->mngt_ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing unregister of OCF vbdev\n",
		      ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to unregister OCF vbdev: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-error));
	}

	ocf_mngt_cache_put(cache);
	vbdev_ocf_core_destroy(core_ctx);

	if (ocf_cache_get_core_count(cache) == ocf_cache_get_core_inactive_count(cache)) {
		/* All cores in this cache were already unregistered
		 * and detached, so proceed with stopping the cache. */
		ocf_mngt_cache_lock(cache, _cache_stop_lock_cb, cache_stop_ctx);
	}
}

static int
_cache_stop_core_visit(ocf_core_t core, void *cb_arg)
{
	struct vbdev_ocf_mngt_ctx *cache_stop_ctx = cb_arg;
	ocf_cache_t cache = ocf_core_get_cache(core);
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': cache stop visit\n", ocf_core_get_name(core));

	if (!core_ctx) {
		/* Skip this core. If there is no context, it means that this core
		 * was added from metadata during cache load and it's just an empty shell. */
		ocf_mngt_cache_put(cache);
		return 0;
	}

	/* If core is detached it's already unregistered, so just free its data and exit. */
	if (!vbdev_ocf_core_is_base_attached(core_ctx)) {
		ocf_mngt_cache_put(cache);
		vbdev_ocf_core_destroy(core_ctx);
		return 0;
	}

	core_ctx->mngt_ctx = cache_stop_ctx;

	if ((rc = vbdev_ocf_core_unregister(core_ctx, _cache_stop_core_unregister_cb, core))) {
		SPDK_ERRLOG("OCF core '%s': failed to start unregistering OCF vbdev: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-rc));
		ocf_mngt_cache_put(cache);
		return rc;
	}

	return rc;
}

static int
_module_fini_cache_visit(ocf_cache_t cache, void *cb_arg)
{
	int i, rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': module stop visit\n", ocf_cache_get_name(cache));

	/* Increment cache refcount to not destroy cache structs before destroying all cores. */
	for (i = ocf_cache_get_core_count(cache); i > 0; i--) {
		if ((rc = ocf_mngt_cache_get(cache))) {
			SPDK_ERRLOG("OCF cache '%s': failed to increment refcount: %s\n",
				    ocf_cache_get_name(cache), spdk_strerror(-rc));
		}
	}

	if (!ocf_cache_get_core_count(cache) ||
			ocf_cache_get_core_count(cache) == ocf_cache_get_core_inactive_count(cache)) {
		/* If there are no cores or all of them are detached,
		 * then cache stop can be triggered already. */
		ocf_mngt_cache_lock(cache, _cache_stop_lock_cb, NULL);
	}

	if ((rc = ocf_core_visit(cache, _cache_stop_core_visit, NULL, false))) {
		SPDK_ERRLOG("OCF cache '%s': failed to iterate over core bdevs: %s\n",
			    ocf_cache_get_name(cache), spdk_strerror(-rc));
		ocf_mngt_cache_lock(cache, _cache_stop_lock_cb, NULL);
	}

	/* In module fini return 0 despite any errors to keep shutting down all caches. */
	return 0;
}

static void
vbdev_ocf_module_fini_start(void)
{
	struct vbdev_ocf_core *core_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF: initiating module stop\n");

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		if (vbdev_ocf_core_is_base_attached(core_ctx)) {
			vbdev_ocf_core_base_detach(core_ctx);
		}
	}

	if (!ocf_mngt_cache_get_count(vbdev_ocf_ctx)) {
		spdk_bdev_module_fini_start_done();
		return;
	}

	if ((rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _module_fini_cache_visit, NULL))) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
		spdk_bdev_module_fini_start_done();
		return;
	}
}

static void
vbdev_ocf_module_fini(void)
{
	struct vbdev_ocf_core *core_ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF: finishing module stop\n");

	while ((core_ctx = STAILQ_FIRST(&g_vbdev_ocf_core_waitlist))) {
		STAILQ_REMOVE(&g_vbdev_ocf_core_waitlist, core_ctx, vbdev_ocf_core, waitlist_entry);
		vbdev_ocf_core_destroy(core_ctx);
	}

	vbdev_ocf_volume_cleanup();
	vbdev_ocf_ctx_cleanup();
}

static int
vbdev_ocf_module_get_ctx_size(void)
{
	return sizeof(struct vbdev_ocf_data);
}

static int
_examine_config_core_visit(ocf_core_t core, void *cb_arg)
{
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	char *bdev_name = cb_arg;
	int rc = 0;

	if (!core_ctx) {
		/* Skip this core. If there is no context, it means that this core
		 * was added from metadata during cache load and it's just an empty shell. */
		return 0;
	}

	if (strcmp(bdev_name, core_ctx->base.name)) {
		return 0;
	}

	SPDK_NOTICELOG("OCF core '%s': base bdev '%s' found\n",
		       ocf_core_get_name(core), bdev_name);

	assert(!vbdev_ocf_core_is_base_attached(core_ctx));

	if ((rc = vbdev_ocf_core_base_attach(core_ctx, bdev_name))) {
		SPDK_ERRLOG("OCF core '%s': failed to attach base bdev '%s'\n",
			    vbdev_ocf_core_get_name(core_ctx), bdev_name);
		return rc;
	}

	/* This whole situation with core being present in cache without base bdev attached
	 * is only possible when core was previously hot removed from SPDK.
	 * In such case it was detached from cache (not removed), so set 'try_add' in core
	 * config to 'true' to indicate that this core is still in cache metadata. */
	core_ctx->core_cfg.try_add = true;

	return -EEXIST;
}

static int
_examine_config_cache_visit(ocf_cache_t cache, void *cb_arg)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	char *bdev_name = cb_arg;
	int rc = 0;

	if (strcmp(bdev_name, cache_ctx->base.name)) {
		return ocf_core_visit(cache, _examine_config_core_visit, bdev_name, false);
	}

	SPDK_NOTICELOG("OCF cache '%s': base bdev '%s' found\n",
		       ocf_cache_get_name(cache), bdev_name);

	assert(!ocf_cache_is_device_attached(cache));
	assert(!vbdev_ocf_cache_is_base_attached(cache));

	if ((rc = vbdev_ocf_cache_base_attach(cache, bdev_name))) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach base bdev '%s'\n",
			    ocf_cache_get_name(cache), bdev_name);
		return rc;
	}

	if ((rc = vbdev_ocf_cache_config_volume_create(cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create config volume\n",
			    ocf_cache_get_name(cache));
		vbdev_ocf_cache_base_detach(cache);
		return rc;
	}

	return -EEXIST;
}

static void
vbdev_ocf_module_examine_config(struct spdk_bdev *bdev)
{
	struct vbdev_ocf_core *core_ctx;
	char *bdev_name = (char *)spdk_bdev_get_name(bdev);
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF: looking for vbdevs waiting for '%s'\n", bdev_name);

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		if (strcmp(bdev_name, core_ctx->base.name)) {
			continue;
		}

		SPDK_NOTICELOG("OCF core '%s': base bdev '%s' found\n",
			       vbdev_ocf_core_get_name(core_ctx), bdev_name);

		assert(!vbdev_ocf_core_is_base_attached(core_ctx));

		if ((rc = vbdev_ocf_core_base_attach(core_ctx, bdev_name))) {
			SPDK_ERRLOG("OCF core '%s': failed to attach base bdev '%s'\n",
				    vbdev_ocf_core_get_name(core_ctx), bdev_name);
		}

		spdk_bdev_module_examine_done(&ocf_if);
		return;
	}

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _examine_config_cache_visit, bdev_name);
	if (rc && rc != -EEXIST) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
	}

	spdk_bdev_module_examine_done(&ocf_if);
}

static void
_core_add_examine_err_cb(void *cb_arg, int error)
{
	ocf_cache_t cache = cb_arg;

	if (error) {
		SPDK_ERRLOG("OCF core: failed to remove OCF core device (OCF error: %d)\n", error);
	}

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	spdk_bdev_module_examine_done(&ocf_if);
}

static void
_core_add_examine_add_cb(ocf_cache_t cache, ocf_core_t core, void *cb_arg, int error)
{
	struct vbdev_ocf_core *core_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx_waitlist;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing add of OCF core\n",
		      vbdev_ocf_core_get_name(core_ctx));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to add core to OCF cache '%s' (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), ocf_cache_get_name(cache), error);
		ocf_mngt_cache_unlock(cache);
		ocf_mngt_cache_put(cache);
		vbdev_ocf_core_base_detach(core_ctx);
		spdk_bdev_module_examine_done(&ocf_if);
		return;
	}

	ocf_core_set_priv(core, core_ctx);

	if ((rc = vbdev_ocf_core_register(core))) {
		SPDK_ERRLOG("OCF core '%s': failed to register vbdev\n", ocf_core_get_name(core));
		ocf_mngt_cache_remove_core(core, _core_add_examine_err_cb, cache);
		vbdev_ocf_core_base_detach(core_ctx); // move to callback (pass core_ctx ?)
		return;
	}

	SPDK_NOTICELOG("OCF core '%s': added to cache '%s'\n",
		       ocf_core_get_name(core), ocf_cache_get_name(cache));

	/* If core was taken from waitlist, remove it from there. */
	vbdev_ocf_foreach_core_in_waitlist(core_ctx_waitlist) {
		if (strcmp(vbdev_ocf_core_get_name(core_ctx), vbdev_ocf_core_get_name(core_ctx_waitlist))) {
			continue;
		}

		STAILQ_REMOVE(&g_vbdev_ocf_core_waitlist, core_ctx, vbdev_ocf_core, waitlist_entry);
	}

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	spdk_bdev_module_examine_done(&ocf_if);
}

static void
_core_add_examine_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_core *core_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating add of OCF core\n",
		      vbdev_ocf_core_get_name(core_ctx));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), error);
		ocf_mngt_cache_put(cache);
		vbdev_ocf_core_base_detach(core_ctx);
		spdk_bdev_module_examine_done(&ocf_if);
		return;
	}

	ocf_mngt_cache_add_core(cache, &core_ctx->core_cfg, _core_add_examine_add_cb, core_ctx);
}

static void
_cache_attach_examine_attach_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *examine_attach_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing device attach\n",
		      ocf_cache_get_name(cache));

	/* At this point volume was either moved to ocf_cache_t struct
	 * or is no longer needed due to some errors, so we need to deallocate it. */
	vbdev_ocf_cache_config_volume_destroy(cache);

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach OCF cache device (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		vbdev_ocf_cache_base_detach(cache);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': device attached\n", ocf_cache_get_name(cache));
	}

	ocf_mngt_cache_unlock(cache);
	free(examine_attach_ctx);
	spdk_bdev_module_examine_done(&ocf_if); // move after adding cores from waitlist ?

	vbdev_ocf_core_add_from_waitlist(cache);
}

static void
_cache_attach_examine_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *examine_attach_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': attaching OCF cache device\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto err_lock;
	}

	examine_attach_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!examine_attach_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for examine attach context: %s\n",
			    ocf_cache_get_name(cache), spdk_strerror(-ENOMEM));
		goto err_alloc;
	}
	examine_attach_ctx->cache = cache;
	examine_attach_ctx->u.att_cb_fn = _cache_attach_examine_attach_cb;

	if ((rc = vbdev_ocf_cache_volume_attach(cache, examine_attach_ctx))) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach volume (OCF error: %d)\n",
			    ocf_cache_get_name(cache), rc);
		goto err_attach;
	}

	return;

err_attach:
	free(examine_attach_ctx);
err_alloc:
	ocf_mngt_cache_unlock(cache);
err_lock:
	vbdev_ocf_cache_config_volume_destroy(cache);
	vbdev_ocf_cache_base_detach(cache);
	spdk_bdev_module_examine_done(&ocf_if);
}

static int
_examine_disk_core_visit(ocf_core_t core, void *cb_arg)
{
	ocf_cache_t cache = ocf_core_get_cache(core);
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	char *bdev_name = cb_arg;

	if (!core_ctx) {
		/* Skip this core. If there is no context, it means that this core
		 * was added from metadata during cache load and it's just an empty shell. */
		return 0;
	}

	if (strcmp(bdev_name, core_ctx->base.name)) {
		return 0;
	}

	/* Get cache once to be in sync with adding core from waitlist scenario. */
	ocf_mngt_cache_get(cache);
	ocf_mngt_cache_lock(cache, _core_add_examine_lock_cb, core_ctx);

	return -EEXIST;
}

static int
_examine_disk_cache_visit(ocf_cache_t cache, void *cb_arg)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	char *bdev_name = cb_arg;

	if (strcmp(bdev_name, cache_ctx->base.name)) {
		return ocf_core_visit(cache, _examine_disk_core_visit, bdev_name, false);
	}

	assert(!ocf_cache_is_device_attached(cache));

	ocf_mngt_cache_lock(cache, _cache_attach_examine_lock_cb, NULL);

	return -EEXIST;
}

static void
vbdev_ocf_module_examine_disk(struct spdk_bdev *bdev)
{
	ocf_cache_t cache;
	struct vbdev_ocf_core *core_ctx;
	char *bdev_name = (char *)spdk_bdev_get_name(bdev);
	int rc;

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		if (strcmp(bdev_name, core_ctx->base.name)) {
			continue;
		}

		if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, core_ctx->cache_name,
					       OCF_CACHE_NAME_SIZE, &cache)) {
			SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for OCF cache '%s'\n",
				       vbdev_ocf_core_get_name(core_ctx), core_ctx->cache_name);
			spdk_bdev_module_examine_done(&ocf_if);
			return;
		}

		if (!ocf_cache_is_device_attached(cache)) {
			SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for OCF cache device '%s'\n",
				       vbdev_ocf_core_get_name(core_ctx),
				       ((struct vbdev_ocf_cache *)ocf_cache_get_priv(cache))->base.name);
			ocf_mngt_cache_put(cache); // ?
			spdk_bdev_module_examine_done(&ocf_if);
			return;
		}

		core_ctx->core_cfg.try_add = vbdev_ocf_core_is_loaded(vbdev_ocf_core_get_name(core_ctx));

		ocf_mngt_cache_lock(cache, _core_add_examine_lock_cb, core_ctx);
		return;
	}

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _examine_disk_cache_visit, bdev_name);
	if (rc && rc != -EEXIST) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
	} else if (!rc) {
		/* No visitor matched this new bdev, so no one called _examine_done(). */
		spdk_bdev_module_examine_done(&ocf_if);
	}
}

static void
_destruct_core_detach_cb(ocf_core_t core, void *cb_arg, int error)
{
	ocf_cache_t cache = cb_arg;
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': finishing detach of OCF core\n", ocf_core_get_name(core));
	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': finishing destruct\n", ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to remove OCF core device (OCF error: %d)\n",
			    ocf_core_get_name(core), error);
		//spdk_bdev_destruct_done(&core->ocf_vbdev, error);
	}

	ocf_mngt_cache_unlock(cache);
	vbdev_ocf_core_base_detach(core_ctx);

	/* This one finally calls the callback from spdk_bdev_unregister_by_name(). */
	spdk_bdev_destruct_done(&core_ctx->ocf_vbdev, 0);
}

static void
_destruct_core_flush_cb(ocf_core_t core, void *cb_arg, int error)
{
	if (error) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to flush OCF core device (OCF error: %d)\n",
			    ocf_core_get_name(core), error);
		//ocf_mngt_cache_unlock(cache);
		//spdk_bdev_destruct_done(&core_ctx->ocf_vbdev, error);
		//return;
	}

	ocf_mngt_cache_detach_core(core, _destruct_core_detach_cb, cb_arg);
}

static void
_destruct_cache_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	ocf_core_t core = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': initiating detach of OCF core\n",
		      ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_core_get_name(core), error);
		//spdk_bdev_destruct_done(&core_ctx->ocf_vbdev, error);
		//return;
	}

	if (ocf_mngt_core_is_dirty(core)) {
		ocf_mngt_core_flush(core, _destruct_core_flush_cb, cache);
	} else {
		ocf_mngt_cache_detach_core(core, _destruct_core_detach_cb, cache);
	}
}

static void
_destruct_io_device_unregister_cb(void *io_device)
{
	ocf_core_t core = io_device;

	ocf_mngt_cache_lock(ocf_core_get_cache(core), _destruct_cache_lock_cb, core);
}

/* This is called internally by SPDK during spdk_bdev_unregister_by_name(). */
static int
vbdev_ocf_fn_destruct(void *ctx)
{
	ocf_core_t core = ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': initiating destruct\n", ocf_core_get_name(core));

	spdk_io_device_unregister(core, _destruct_io_device_unregister_cb);

	/* Return one to indicate async destruct. */
	return 1;
}

static void
_vbdev_ocf_submit_io_cb(ocf_io_t io, void *priv1, void *priv2, int error)
{
	struct spdk_bdev_io *bdev_io = priv1;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': finishing submit of IO request\n",
		      spdk_bdev_get_name(bdev_io->bdev));

	ocf_io_put(io);

	if (error == -OCF_ERR_NO_MEM) {
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_NOMEM);
	} else if (error) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to complete OCF IO\n",
			    spdk_bdev_get_name(bdev_io->bdev));
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
	} else {
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_SUCCESS);
	}
}

typedef void (*submit_io_to_ocf_fn)(ocf_io_t io);

static void
vbdev_ocf_submit_io(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io, uint64_t offset,
		    uint32_t len, uint32_t dir, uint64_t flags, submit_io_to_ocf_fn submit_io_fn)
{
	ocf_core_t core = bdev_io->bdev->ctxt;
	struct vbdev_ocf_data *data = (struct vbdev_ocf_data *)bdev_io->driver_ctx;
	struct vbdev_ocf_core_io_channel_ctx *ch_ctx = spdk_io_channel_get_ctx(ch);
	ocf_io_t io = NULL;

	// impossible to be true ?
	if (!core) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to submit IO - no OCF core device\n",
			    spdk_bdev_get_name(bdev_io->bdev));
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
		return;
	}
	
	io = ocf_volume_new_io(ocf_core_get_front_volume(core), ch_ctx->queue,
			       offset, len, dir, 0, flags);
	if (!io) {
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_NOMEM);
		return;
	}

	data->iovs = bdev_io->u.bdev.iovs;
	data->iovcnt = bdev_io->u.bdev.iovcnt;
	data->size = bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen;

	ocf_io_set_data(io, data, 0);
	ocf_io_set_cmpl(io, bdev_io, NULL, _vbdev_ocf_submit_io_cb);
	submit_io_fn(io);
}

static void
_io_read_get_buf_cb(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io, bool success)
{
	uint64_t offset = bdev_io->u.bdev.offset_blocks * bdev_io->bdev->blocklen;
	uint32_t len = bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen;

	if (!success) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to allocate IO buffer - size of the "
			    "buffer to allocate might be greater than the permitted maximum\n",
			    spdk_bdev_get_name(bdev_io->bdev));
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
		return;
	}

	vbdev_ocf_submit_io(ch, bdev_io, offset, len, OCF_READ, 0, ocf_core_submit_io);
}

static void
vbdev_ocf_fn_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
	uint64_t offset = bdev_io->u.bdev.offset_blocks * bdev_io->bdev->blocklen;
	uint32_t len = bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': initiating submit of IO request\n",
		      spdk_bdev_get_name(bdev_io->bdev));

	switch (bdev_io->type) {
	case SPDK_BDEV_IO_TYPE_READ:
		// align buffer for write as well ? (comment in old vbdev_ocf.c)
		// from doc: This function *must* be called from the thread issuing bdev_io.
		spdk_bdev_io_get_buf(bdev_io, _io_read_get_buf_cb, len);
		break;
	case SPDK_BDEV_IO_TYPE_WRITE:
		vbdev_ocf_submit_io(ch, bdev_io, offset, len, OCF_WRITE, 0, ocf_core_submit_io);
		break;
	case SPDK_BDEV_IO_TYPE_UNMAP:
		vbdev_ocf_submit_io(ch, bdev_io, offset, len, OCF_WRITE, 0, ocf_core_submit_discard);
		break;
	case SPDK_BDEV_IO_TYPE_FLUSH:
		vbdev_ocf_submit_io(ch, bdev_io, 0, 0, OCF_WRITE, OCF_WRITE_FLUSH, ocf_core_submit_flush);
		break;
	default:
		SPDK_ERRLOG("OCF vbdev '%s': unsupported IO type: %s\n", spdk_bdev_get_name(bdev_io->bdev),
			    spdk_bdev_get_io_type_name(bdev_io->type));
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
	}
}

static bool
vbdev_ocf_fn_io_type_supported(void *ctx, enum spdk_bdev_io_type io_type)
{
	ocf_core_t core = ctx;
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': checking if IO type '%s' is supported\n",
		      spdk_bdev_get_name(&core_ctx->ocf_vbdev), spdk_bdev_get_io_type_name(io_type));

	switch (io_type) {
	case SPDK_BDEV_IO_TYPE_READ:
	case SPDK_BDEV_IO_TYPE_WRITE:
	case SPDK_BDEV_IO_TYPE_UNMAP:
	case SPDK_BDEV_IO_TYPE_FLUSH:
		return spdk_bdev_io_type_supported(core_ctx->base.bdev, io_type);
	default:
		return false;
	}
}

static struct spdk_io_channel *
vbdev_ocf_fn_get_io_channel(void *ctx) // ctx == ocf_vbdev.ctxt
{
	ocf_core_t core = ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': got request for IO channel\n",
		      spdk_bdev_get_name(&(((struct vbdev_ocf_core *)ocf_core_get_priv(core))->ocf_vbdev)));

	return spdk_get_io_channel(core);
}

static int
vbdev_ocf_fn_dump_info_json(void *ctx, struct spdk_json_write_ctx *w)
{
	return 0;
}

static void
vbdev_ocf_fn_write_config_json(struct spdk_bdev *bdev, struct spdk_json_write_ctx *w)
{
}

static void
_cache_start_rpc_err_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to stop OCF cache properly (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	}

	vbdev_ocf_cache_destroy(cache);
	ocf_mngt_cache_unlock(cache); // use in error path only ?
	//ocf_mngt_cache_put(cache); // no need ? (check refcnt)
}

static void
_cache_start_rpc_attach_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_start_ctx = cb_arg;
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing start\n", ocf_cache_get_name(cache));

	/* At this point volume was either moved to ocf_cache_t struct
	 * or is no longer needed due to some errors, so we need to deallocate it. */
	vbdev_ocf_cache_config_volume_destroy(cache);

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach OCF cache device\n",
			    ocf_cache_get_name(cache));

		if (error == -OCF_ERR_NO_MEM) {
			uint64_t mem_needed, volume_size;

			volume_size = cache_ctx->base.bdev->blockcnt * cache_ctx->base.bdev->blocklen;
			mem_needed = ocf_mngt_get_ram_needed(cache, volume_size);
			SPDK_ERRLOG("Not enough memory. Try to increase hugepage memory size or cache line size.\n");
			SPDK_NOTICELOG("Needed memory to start cache in this configuration "
				       "(device size: %"PRIu64", cache line size: %"PRIu64"): %"PRIu64"\n",
				       volume_size, cache_ctx->cache_cfg.cache_line_size, mem_needed);
		}

		vbdev_ocf_cache_base_detach(cache);
		ocf_queue_put(cache_ctx->cache_mngt_q); // needed ?
		ocf_mngt_cache_stop(cache, _cache_start_rpc_err_cb, NULL);
		cache_start_ctx->rpc_cb_fn(NULL, cache_start_ctx->rpc_cb_arg, error); // cache name
		free(cache_start_ctx);

		return;
	}

	SPDK_NOTICELOG("OCF cache '%s': started\n", ocf_cache_get_name(cache));
	
	ocf_mngt_cache_unlock(cache);
	cache_start_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cache_start_ctx->rpc_cb_arg, 0);
	free(cache_start_ctx);

	vbdev_ocf_core_add_from_waitlist(cache);
}

/* RPC entry point. */
void
vbdev_ocf_cache_start(const char *cache_name, const char *base_name,
		      const char *cache_mode, const uint8_t cache_line_size, bool no_load,
		      vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *cache_start_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating start\n", cache_name);

	if (vbdev_ocf_bdev_exists(cache_name)) {
		SPDK_ERRLOG("OCF: bdev '%s' already exists\n", cache_name);
		rc = -EEXIST;
		goto err_exist;
	}

	if ((rc = vbdev_ocf_cache_create(&cache, cache_name, cache_mode,
					 cache_line_size, no_load))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create cache\n", cache_name);
		goto err_create;
	}

	if ((rc = vbdev_ocf_cache_mngt_queue_create(cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create management queue\n", cache_name);
		goto err_queue;
	}

	if ((rc = vbdev_ocf_cache_base_attach(cache, base_name))) {
		if (rc == -ENODEV) {
			SPDK_NOTICELOG("OCF cache '%s': start deferred - waiting for base bdev '%s'\n",
				       cache_name, base_name);
			ocf_mngt_cache_unlock(cache);
			rpc_cb_fn(cache_name, rpc_cb_arg, -ENODEV);
			return;
		}
		SPDK_ERRLOG("OCF cache '%s': failed to attach base bdev '%s'\n", cache_name, base_name);
		goto err_base;
	}

	if ((rc = vbdev_ocf_cache_config_volume_create(cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create config volume\n", cache_name);
		goto err_volume;
	}

	cache_start_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!cache_start_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache start context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	cache_start_ctx->rpc_cb_fn = rpc_cb_fn;
	cache_start_ctx->rpc_cb_arg = rpc_cb_arg;
	cache_start_ctx->cache = cache;
	cache_start_ctx->u.att_cb_fn = _cache_start_rpc_attach_cb;

	if ((rc = vbdev_ocf_cache_volume_attach(cache, cache_start_ctx))) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach volume\n", cache_name);
		goto err_attach;
	}

	return;

err_attach:
	free(cache_start_ctx);
err_alloc:
	vbdev_ocf_cache_config_volume_destroy(cache);
err_volume:
	vbdev_ocf_cache_base_detach(cache);
err_base:
	ocf_queue_put(((struct vbdev_ocf_cache *)ocf_cache_get_priv(cache))->cache_mngt_q);
err_queue:
	ocf_mngt_cache_stop(cache, _cache_start_rpc_err_cb, NULL);
err_create:
err_exist:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

/* RPC entry point. */
void
vbdev_ocf_cache_stop(const char *cache_name, vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *cache_stop_ctx;
	int i, rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating stop\n", cache_name);

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	// DONE ?
	// TODO: send hotremove to each core opener first - check how spdk_bdev_unregister does that
	// take from bdev_unregister_unsafe()
	// or stop cache after unregister ?

	cache_stop_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!cache_stop_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache stop context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	cache_stop_ctx->cache = cache;
	cache_stop_ctx->rpc_cb_fn = rpc_cb_fn;
	cache_stop_ctx->rpc_cb_arg = rpc_cb_arg;

	/* Increment cache refcount to not destroy cache structs before destroying all cores. */
	for (i = ocf_cache_get_core_count(cache); i > 0; i--) {
		if ((rc = ocf_mngt_cache_get(cache))) {
			SPDK_ERRLOG("OCF cache '%s': failed to increment refcount\n",
				    ocf_cache_get_name(cache));
			for (i = ocf_cache_get_core_count(cache) - i; i > 0; i--) {
				ocf_mngt_cache_put(cache);
			}
			goto err_get;
		}
	}

	if (!ocf_cache_get_core_count(cache) ||
			ocf_cache_get_core_count(cache) == ocf_cache_get_core_inactive_count(cache)) {
		/* If there are no cores or all of them are detached,
		 * then cache stop can be triggered already. */
		ocf_mngt_cache_lock(cache, _cache_stop_lock_cb, cache_stop_ctx);
	}

	if ((rc = ocf_core_visit(cache, _cache_stop_core_visit, cache_stop_ctx, false))) {
		SPDK_ERRLOG("OCF cache '%s': failed to iterate over core bdevs\n",
			    ocf_cache_get_name(cache));
		// ocf_mngt_cache_put() ? how many times ?
		goto err_visit;
	}

	return;

err_visit:
err_get:
	free(cache_stop_ctx);
err_alloc:
	ocf_mngt_cache_put(cache);
err_cache:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_cache_detach_rpc_detach_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_detach_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing device detach\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to detach OCF cache device (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	} else {
		vbdev_ocf_cache_base_detach(cache);
		SPDK_NOTICELOG("OCF cache '%s': device detached\n", ocf_cache_get_name(cache));
	}

	/* Increment queue refcount to prevent destroying management queue after cache device detach. */
	ocf_queue_get(((struct vbdev_ocf_cache *)ocf_cache_get_priv(cache))->cache_mngt_q);

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	cache_detach_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cache_detach_ctx->rpc_cb_arg, error);
	free(cache_detach_ctx);
}

static void
_cache_detach_rpc_flush_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_detach_ctx = cb_arg;

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to flush OCF cache (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		ocf_mngt_cache_unlock(cache);
		ocf_mngt_cache_put(cache);
		cache_detach_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cache_detach_ctx->rpc_cb_arg, error);
		free(cache_detach_ctx);
		return;
	}

	ocf_mngt_cache_detach(cache, _cache_detach_rpc_detach_cb, cache_detach_ctx);
}

static void
_cache_detach_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_detach_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': detaching OCF cache device\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		ocf_mngt_cache_put(cache);
		cache_detach_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cache_detach_ctx->rpc_cb_arg, error);
		free(cache_detach_ctx);
		return;
	}

	// no need to check dirty ?
	if (ocf_mngt_cache_is_dirty(cache)) {
		ocf_mngt_cache_flush(cache, _cache_detach_rpc_flush_cb, cache_detach_ctx);
	} else {
		ocf_mngt_cache_detach(cache, _cache_detach_rpc_detach_cb, cache_detach_ctx);
	}
}

/* RPC entry point. */
void
vbdev_ocf_cache_detach(const char *cache_name, vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *cache_detach_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating device detach\n", cache_name);

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	if (!ocf_cache_is_device_attached(cache)) {
		SPDK_ERRLOG("OCF cache '%s': device already detached\n", cache_name);
		rc = -EALREADY; // better errno ?
		goto err_state;
	}

	cache_detach_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!cache_detach_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache detach context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	cache_detach_ctx->rpc_cb_fn = rpc_cb_fn;
	cache_detach_ctx->rpc_cb_arg = rpc_cb_arg;

	ocf_mngt_cache_lock(cache, _cache_detach_rpc_lock_cb, cache_detach_ctx);

	return;

err_alloc:
err_state:
	ocf_mngt_cache_put(cache);
err_cache:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_cache_attach_rpc_attach_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_attach_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing device attach\n",
		      ocf_cache_get_name(cache));

	/* At this point volume was either moved to ocf_cache_t struct
	 * or is no longer needed due to some errors, so we need to deallocate it. */
	vbdev_ocf_cache_config_volume_destroy(cache);

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach OCF cache device (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		vbdev_ocf_cache_base_detach(cache);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': device attached\n", ocf_cache_get_name(cache));
	}

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	cache_attach_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cache_attach_ctx->rpc_cb_arg, error);
	free(cache_attach_ctx);

	vbdev_ocf_core_add_from_waitlist(cache);
}

static void
_cache_attach_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_attach_ctx = cb_arg;
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': attaching OCF cache device\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		vbdev_ocf_cache_config_volume_destroy(cache);
		vbdev_ocf_cache_base_detach(cache);
		ocf_mngt_cache_put(cache);
		cache_attach_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cache_attach_ctx->rpc_cb_arg, error);
		free(cache_attach_ctx);
		return;
	}

	ocf_mngt_cache_attach(cache, &cache_ctx->cache_att_cfg, _cache_attach_rpc_attach_cb,
			      cache_attach_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_cache_attach(const char *cache_name, const char *base_name, bool force,
		       vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_cache *cache_ctx;
	struct vbdev_ocf_mngt_ctx *cache_attach_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating device attach\n", cache_name);

	// ocf_mngt_cache_put() right away to merge cache_start/attach callbacks ?
	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	if (!ocf_cache_is_detached(cache)) {
		SPDK_ERRLOG("OCF cache '%s': device already attached\n", cache_name);
		rc = -EEXIST; // better errno ?
		goto err_state;
	}

	cache_ctx = ocf_cache_get_priv(cache);
	cache_ctx->no_load = force;

	if ((rc = vbdev_ocf_cache_base_attach(cache, base_name))) {
		if (rc == -ENODEV) {
			SPDK_NOTICELOG("OCF cache '%s': start deferred - waiting for base bdev '%s'\n",
				       cache_name, base_name);
			rpc_cb_fn(cache_name, rpc_cb_arg, -ENODEV);
			return;
		}
		SPDK_ERRLOG("OCF cache '%s': failed to attach base bdev '%s'\n", cache_name, base_name);
		goto err_base;
	}

	if ((rc = vbdev_ocf_cache_config_volume_create(cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create config volume\n", cache_name);
		goto err_volume;
	}

	cache_attach_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!cache_attach_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache attach context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	cache_attach_ctx->rpc_cb_fn = rpc_cb_fn;
	cache_attach_ctx->rpc_cb_arg = rpc_cb_arg;

	ocf_mngt_cache_lock(cache, _cache_attach_rpc_lock_cb, cache_attach_ctx);

	return;

err_alloc:
	vbdev_ocf_cache_config_volume_destroy(cache);
err_volume:
	vbdev_ocf_cache_base_detach(cache);
err_base:
err_state:
	ocf_mngt_cache_put(cache);
err_cache:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_core_add_rpc_err_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *core_add_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = core_add_ctx->u.core_ctx;
	ocf_cache_t cache = core_add_ctx->cache;

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to remove OCF core device (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), error);
	}

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	vbdev_ocf_core_base_detach(core_ctx);
	vbdev_ocf_core_destroy(core_ctx);
	core_add_ctx->rpc_cb_fn(NULL, core_add_ctx->rpc_cb_arg, error);
	free(core_add_ctx);
}

static void
_core_add_rpc_add_cb(ocf_cache_t cache, ocf_core_t core, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *core_add_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = core_add_ctx->u.core_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing add of OCF core\n",
		      vbdev_ocf_core_get_name(core_ctx));
	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing add\n", vbdev_ocf_core_get_name(core_ctx));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to add core to OCF cache '%s'\n",
			    vbdev_ocf_core_get_name(core_ctx), ocf_cache_get_name(cache));
		ocf_mngt_cache_unlock(cache);
		ocf_mngt_cache_put(cache);
		vbdev_ocf_core_base_detach(core_ctx);
		vbdev_ocf_core_destroy(core_ctx);
		core_add_ctx->rpc_cb_fn(NULL, core_add_ctx->rpc_cb_arg, error);
		free(core_add_ctx);
		return;
	}

	ocf_core_set_priv(core, core_ctx);

	if ((rc = vbdev_ocf_core_register(core))) {
		SPDK_ERRLOG("OCF core '%s': failed to register vbdev\n", ocf_core_get_name(core));
		ocf_mngt_cache_remove_core(core, _core_add_rpc_err_cb, core_add_ctx);
		return;
	}

	SPDK_NOTICELOG("OCF core '%s': added to cache '%s'\n",
		       ocf_core_get_name(core), ocf_cache_get_name(cache));

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	core_add_ctx->rpc_cb_fn(ocf_core_get_name(core), core_add_ctx->rpc_cb_arg, rc);
	free(core_add_ctx);
}

static void
_core_add_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *core_add_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = core_add_ctx->u.core_ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating add of OCF core\n",
		      vbdev_ocf_core_get_name(core_ctx));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to acquire OCF cache lock\n",
			    vbdev_ocf_core_get_name(core_ctx));
		ocf_mngt_cache_put(cache);
		vbdev_ocf_core_base_detach(core_ctx);
		vbdev_ocf_core_destroy(core_ctx);
		core_add_ctx->rpc_cb_fn(NULL, core_add_ctx->rpc_cb_arg, error); // core name
		free(core_add_ctx);
		return;
	}

	ocf_mngt_cache_add_core(cache, &core_ctx->core_cfg, _core_add_rpc_add_cb, core_add_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_core_add(const char *core_name, const char *base_name, const char *cache_name,
		   vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_cache *cache_ctx;
	struct vbdev_ocf_core *core_ctx;
	struct vbdev_ocf_mngt_ctx *core_add_ctx;
	uint32_t cache_block_size, core_block_size;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating add\n", core_name);

	if (vbdev_ocf_bdev_exists(core_name)) {
		SPDK_ERRLOG("OCF: bdev '%s' already exists\n", core_name);
		rc = -EEXIST;
		goto err_exist;
	}

	if ((rc = vbdev_ocf_core_create(&core_ctx, core_name, cache_name))) {
		SPDK_ERRLOG("OCF core '%s': failed to create core\n", core_name);
		goto err_create;
	}

	/* First, check if base device for this core is already present. */
	if ((rc = vbdev_ocf_core_base_attach(core_ctx, base_name))) {
		if (rc == -ENODEV) {
			/* If not, just put core context on the temporary core wait list and exit. */
			SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for base bdev '%s'\n",
				       core_name, base_name);
			STAILQ_INSERT_TAIL(&g_vbdev_ocf_core_waitlist, core_ctx, waitlist_entry);
			rpc_cb_fn(core_name, rpc_cb_arg, -ENODEV);
			return;
		}
		SPDK_ERRLOG("OCF core '%s': failed to attach base bdev '%s'\n", core_name, base_name);
		goto err_base;
	}

	/* Second, check if OCF cache for this core is already started. */
	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		/* If not, just put core context on the temporary core wait list and exit. */
		SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for OCF cache '%s'\n",
			       core_name, cache_name);
		STAILQ_INSERT_TAIL(&g_vbdev_ocf_core_waitlist, core_ctx, waitlist_entry);
		rpc_cb_fn(core_name, rpc_cb_arg, -ENODEV);
		return;
	}

	cache_ctx = ocf_cache_get_priv(cache);

	/* And finally, check if OCF cache device is already attached.
	 * We need to have cache device attached to know if cache was loaded or attached
	 * and then set 'try_add' in core config accordingly. */
	if (!ocf_cache_is_device_attached(cache)) {
		SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for OCF cache device '%s'\n",
			       core_name, cache_ctx->base.name);
		ocf_mngt_cache_put(cache);
		STAILQ_INSERT_TAIL(&g_vbdev_ocf_core_waitlist, core_ctx, waitlist_entry);
		rpc_cb_fn(core_name, rpc_cb_arg, -ENODEV);
		return;
	}

	cache_block_size = cache_ctx->base.bdev->blocklen;
	core_block_size = core_ctx->base.bdev->blocklen;
	if (cache_block_size > core_block_size) {
		SPDK_ERRLOG("OCF core '%s': block size (%d) is less than cache '%s' block size (%d)\n",
			    core_name, core_block_size, cache_name, cache_block_size);
		rc = -ENOTSUP;
		goto err_bsize;
	}

	// DONE
	// find a better way to check if cache was loaded or attached
	//core_ctx->core_cfg.try_add = cache_ctx->cache_att_cfg.force ? false : true;
	core_ctx->core_cfg.try_add = vbdev_ocf_core_is_loaded(core_name);

	// open (and then close) cache-load-added core volumes (with 'base' to save it in priv) ?

	core_add_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!core_add_ctx) {
		SPDK_ERRLOG("OCF core '%s': failed to allocate memory for core add context\n",
			    core_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	core_add_ctx->rpc_cb_fn = rpc_cb_fn;
	core_add_ctx->rpc_cb_arg = rpc_cb_arg;
	core_add_ctx->cache = cache;
	core_add_ctx->u.core_ctx = core_ctx;

	ocf_mngt_cache_lock(cache, _core_add_rpc_lock_cb, core_add_ctx);

	return;

err_alloc:
err_bsize:
	ocf_mngt_cache_put(cache);
	vbdev_ocf_core_base_detach(core_ctx);
err_base:
	vbdev_ocf_core_destroy(core_ctx);
err_create:
err_exist:
	rpc_cb_fn(core_name, rpc_cb_arg, rc);
}

static void
_core_remove_rpc_remove_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *core_rm_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = core_rm_ctx->u.core_ctx;
	ocf_cache_t cache = core_rm_ctx->cache;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing remove of OCF core\n",
		      vbdev_ocf_core_get_name(core_ctx));
	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing removal\n",
		      vbdev_ocf_core_get_name(core_ctx));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to remove OCF core device (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), error);
	} else {
		SPDK_NOTICELOG("OCF core '%s': removed from cache '%s'\n",
			       vbdev_ocf_core_get_name(core_ctx), ocf_cache_get_name(cache));
	}

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	vbdev_ocf_core_destroy(core_ctx);
	core_rm_ctx->rpc_cb_fn(NULL, core_rm_ctx->rpc_cb_arg, error); // core name
	free(core_rm_ctx);
}

static void
_core_remove_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *core_rm_ctx = cb_arg;
	ocf_core_t core = core_rm_ctx->core;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating remove of OCF core\n",
		      ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_core_get_name(core), error);
	}

	ocf_mngt_cache_remove_core(core, _core_remove_rpc_remove_cb, core_rm_ctx);
}

static void
_core_remove_rpc_unregister_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *core_rm_ctx = cb_arg;
	ocf_core_t core = core_rm_ctx->core;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing unregister of OCF vbdev\n",
		      ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to unregister OCF vbdev during core removal\n",
			    ocf_core_get_name(core));
	}

	ocf_mngt_cache_lock(ocf_core_get_cache(core), _core_remove_rpc_lock_cb, core_rm_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_core_remove(const char *core_name, const char *cache_name,
		      vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	ocf_core_t core;
	struct vbdev_ocf_core *core_ctx;
	struct vbdev_ocf_mngt_ctx *core_rm_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating removal\n", core_name);

	/* If core was not added yet due to lack of base or cache device,
	 * just free its structs (and detach its base if exists) and exit. */
	core_ctx = vbdev_ocf_core_waitlist_get_by_name(core_name);
	if (core_ctx) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': removing from wait list\n", core_name);

		STAILQ_REMOVE(&g_vbdev_ocf_core_waitlist, core_ctx, vbdev_ocf_core, waitlist_entry);
		if (vbdev_ocf_core_is_base_attached(core_ctx)) {
			vbdev_ocf_core_base_detach(core_ctx);
		}
		vbdev_ocf_core_destroy(core_ctx);
		rpc_cb_fn(core_name, rpc_cb_arg, 0);
		return;
	}

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	if ((rc = ocf_core_get_by_name(cache, core_name, OCF_CORE_NAME_SIZE, &core))) {
		if (rc == -OCF_ERR_CORE_NOT_EXIST) {
			SPDK_ERRLOG("OCF core '%s': not exist within cache '%s'\n", core_name, cache_name);
			rc = -ENXIO;
		}
		goto err_core;
	}

	core_ctx = ocf_core_get_priv(core);
	if (!core_ctx) {
		/* Skip this core. If there is no context, it means that this core
		 * was added from metadata during cache load and it's just an empty shell. */
		SPDK_ERRLOG("OCF core '%s': not exist within cache '%s'\n", core_name, cache_name);
		rc = -ENXIO;
		goto err_ctx;
	}

	core_rm_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!core_rm_ctx) {
		SPDK_ERRLOG("OCF core '%s': failed to allocate memory for core remove context\n", core_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	core_rm_ctx->rpc_cb_fn = rpc_cb_fn;
	core_rm_ctx->rpc_cb_arg = rpc_cb_arg;
	core_rm_ctx->cache = cache;
	core_rm_ctx->core = core;
	core_rm_ctx->u.core_ctx = core_ctx;

	if (!vbdev_ocf_core_is_base_attached(core_ctx)) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': removing detached (no unregister)\n", core_name);

		ocf_mngt_cache_lock(cache, _core_remove_rpc_lock_cb, core_rm_ctx);
		return;
	}

	if ((rc = vbdev_ocf_core_unregister(core_ctx, _core_remove_rpc_unregister_cb, core_rm_ctx))) {
		SPDK_ERRLOG("OCF core '%s': failed to start unregistering OCF vbdev during core removal\n",
			    ocf_core_get_name(core));
		goto err_unregister;
	}

	return;

err_unregister:
	free(core_rm_ctx);
err_alloc:
err_ctx:
err_core:
	ocf_mngt_cache_put(cache);
err_cache:
	rpc_cb_fn(core_name, rpc_cb_arg, rc);
}

static void
_cache_save_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': saving cache state\n", ocf_cache_get_name(cache));

	ocf_mngt_cache_unlock(cache);

	if (error) {
		SPDK_WARNLOG("OCF cache '%s': failed to save cache state (OCF error: %d)\n",
			     ocf_cache_get_name(cache), error);
	}

	/* Ignore state save error caused by not attached cache volume. */
	mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg,
			    error == -OCF_ERR_CACHE_DETACHED ? 0 : error);
	ocf_mngt_cache_put(cache);
	free(mngt_ctx);
}

static void
_cache_mode_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_mode_ctx = cb_arg;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto err;
	}

	if ((rc = ocf_mngt_cache_set_mode(cache, cache_mode_ctx->u.cache_mode))) {
		SPDK_ERRLOG("OCF cache '%s': failed to change cache mode to '%s' (OCF error: %d)\n",
			    ocf_cache_get_name(cache),
			    vbdev_ocf_cachemode_get_name(cache_mode_ctx->u.cache_mode), rc);
		ocf_mngt_cache_unlock(cache);
		goto err;
	}

	ocf_mngt_cache_save(cache, _cache_save_cb, cache_mode_ctx);

	return;

err:
	cache_mode_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cache_mode_ctx->rpc_cb_arg, rc);
	ocf_mngt_cache_put(cache);
	free(cache_mode_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_set_cachemode(const char *cache_name, const char *cache_mode,
			vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *cache_mode_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': setting new cache mode '%s'\n", cache_name, cache_mode);

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	cache_mode_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!cache_mode_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache mode change context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	cache_mode_ctx->u.cache_mode = vbdev_ocf_cachemode_get_by_name(cache_mode);
	cache_mode_ctx->rpc_cb_fn = rpc_cb_fn;
	cache_mode_ctx->rpc_cb_arg = rpc_cb_arg;

	ocf_mngt_cache_lock(cache, _cache_mode_lock_cb, cache_mode_ctx);

	return;

err_alloc:
	ocf_mngt_cache_put(cache);
err_cache:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_promotion_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto err_lock;
	}

	if (mngt_ctx->u.promotion.policy >= ocf_promotion_always &&
	    mngt_ctx->u.promotion.policy < ocf_promotion_max) {
		if ((rc = ocf_mngt_cache_promotion_set_policy(cache, mngt_ctx->u.promotion.policy))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set promotion policy (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (mngt_ctx->u.promotion.nhit_insertion_threshold >= 0) {
		if ((rc = ocf_mngt_cache_promotion_set_param(cache, ocf_promotion_nhit, ocf_nhit_insertion_threshold,
							    mngt_ctx->u.promotion.nhit_insertion_threshold))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set promotion nhit_insertion_threshold param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (mngt_ctx->u.promotion.nhit_trigger_threshold >= 0) {
		if ((rc = ocf_mngt_cache_promotion_set_param(cache, ocf_promotion_nhit, ocf_nhit_trigger_threshold,
							    mngt_ctx->u.promotion.nhit_trigger_threshold))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set promotion nhit_trigger_threshold param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	ocf_mngt_cache_save(cache, _cache_save_cb, mngt_ctx);

	return;

err_param:
	ocf_mngt_cache_unlock(cache);
err_lock:
	mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, rc);
	ocf_mngt_cache_put(cache);
	free(mngt_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_set_promotion(const char *cache_name, const char *policy,
			int32_t nhit_insertion_threshold, int32_t nhit_trigger_threshold,
			vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': setting promotion params\n", cache_name);

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for promotion set context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->u.promotion.policy = vbdev_ocf_promotion_policy_get_by_name(policy);
	mngt_ctx->u.promotion.nhit_insertion_threshold = nhit_insertion_threshold;
	mngt_ctx->u.promotion.nhit_trigger_threshold = nhit_trigger_threshold;

	ocf_mngt_cache_lock(cache, _promotion_lock_cb, mngt_ctx);

	return;

err_alloc:
	ocf_mngt_cache_put(cache);
err_cache:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_cleaning_policy_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cleaning_ctx = cb_arg;
	ocf_cache_t cache = cleaning_ctx->cache;

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to set cleaning policy (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		ocf_mngt_cache_unlock(cache);
		cleaning_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cleaning_ctx->rpc_cb_arg, error);
		ocf_mngt_cache_put(cache);
		free(cleaning_ctx);
		return;
	}

	ocf_mngt_cache_save(cache, _cache_save_cb, cleaning_ctx);
}

static void
_cleaning_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cleaning_ctx = cb_arg;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto err_lock;
	}

	if (cleaning_ctx->u.cleaning.acp_wake_up_time >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_acp, ocf_acp_wake_up_time,
							    cleaning_ctx->u.cleaning.acp_wake_up_time))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning acp_wake_up_time param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (cleaning_ctx->u.cleaning.acp_flush_max_buffers >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_acp, ocf_acp_flush_max_buffers,
							    cleaning_ctx->u.cleaning.acp_flush_max_buffers))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning acp_flush_max_buffers param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (cleaning_ctx->u.cleaning.alru_wake_up_time >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_wake_up_time,
							    cleaning_ctx->u.cleaning.alru_wake_up_time))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_wake_up_time param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (cleaning_ctx->u.cleaning.alru_flush_max_buffers >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_flush_max_buffers,
							    cleaning_ctx->u.cleaning.alru_flush_max_buffers))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_flush_max_buffers param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (cleaning_ctx->u.cleaning.alru_staleness_time >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_stale_buffer_time,
							    cleaning_ctx->u.cleaning.alru_staleness_time))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_staleness_time param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (cleaning_ctx->u.cleaning.alru_activity_threshold >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_activity_threshold,
							    cleaning_ctx->u.cleaning.alru_activity_threshold))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_activity_threshold param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (cleaning_ctx->u.cleaning.alru_max_dirty_ratio >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_max_dirty_ratio,
							    cleaning_ctx->u.cleaning.alru_max_dirty_ratio))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_max_dirty_ratio param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (cleaning_ctx->u.cleaning.policy >= ocf_cleaning_nop &&
	    cleaning_ctx->u.cleaning.policy < ocf_cleaning_max) {
		ocf_mngt_cache_cleaning_set_policy(cache, cleaning_ctx->u.cleaning.policy,
						   _cleaning_policy_cb, cleaning_ctx);
	} else {
		ocf_mngt_cache_save(cache, _cache_save_cb, cleaning_ctx);
	}

	return;

err_param:
	ocf_mngt_cache_unlock(cache);
err_lock:
	cleaning_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cleaning_ctx->rpc_cb_arg, rc);
	ocf_mngt_cache_put(cache);
	free(cleaning_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_set_cleaning(const char *cache_name, const char *policy, int32_t acp_wake_up_time,
		       int32_t acp_flush_max_buffers, int32_t alru_wake_up_time,
		       int32_t alru_flush_max_buffers, int32_t alru_staleness_time,
		       int32_t alru_activity_threshold, int32_t alru_max_dirty_ratio,
		       vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *cleaning_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': setting cleaning params\n", cache_name);

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	cleaning_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!cleaning_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cleaning set context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	cleaning_ctx->u.cleaning.policy = vbdev_ocf_cleaning_policy_get_by_name(policy);
	cleaning_ctx->u.cleaning.acp_wake_up_time = acp_wake_up_time;
	cleaning_ctx->u.cleaning.acp_flush_max_buffers = acp_flush_max_buffers;
	cleaning_ctx->u.cleaning.alru_wake_up_time = alru_wake_up_time;
	cleaning_ctx->u.cleaning.alru_flush_max_buffers = alru_flush_max_buffers;
	cleaning_ctx->u.cleaning.alru_staleness_time = alru_staleness_time;
	cleaning_ctx->u.cleaning.alru_activity_threshold = alru_activity_threshold;
	cleaning_ctx->u.cleaning.alru_max_dirty_ratio = alru_max_dirty_ratio;
	cleaning_ctx->cache = cache;
	cleaning_ctx->rpc_cb_fn = rpc_cb_fn;
	cleaning_ctx->rpc_cb_arg = rpc_cb_arg;

	ocf_mngt_cache_lock(cache, _cleaning_lock_cb, cleaning_ctx);

	return;

err_alloc:
	ocf_mngt_cache_put(cache);
err_cache:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_seqcutoff_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto err_lock;
	}

	if (mngt_ctx->core) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': setting sequential cut-off on core device\n",
			      mngt_ctx->bdev_name);

		if (mngt_ctx->u.seqcutoff.policy >= ocf_seq_cutoff_policy_always &&
		    mngt_ctx->u.seqcutoff.policy < ocf_seq_cutoff_policy_max) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_policy(mngt_ctx->core,
								      mngt_ctx->u.seqcutoff.policy))) {
				SPDK_ERRLOG("OCF core '%s': failed to set sequential cut-off policy (OCF error: %d)\n",
					    ocf_core_get_name(mngt_ctx->core), rc);
				goto err_param;
			}
		}

		if (mngt_ctx->u.seqcutoff.threshold >= 0) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_threshold(mngt_ctx->core,
									 mngt_ctx->u.seqcutoff.threshold * KiB))) {
				SPDK_ERRLOG("OCF core '%s': failed to set sequential cut-off threshold (OCF error: %d)\n",
					    ocf_core_get_name(mngt_ctx->core), rc);
				goto err_param;
			}
		}

		if (mngt_ctx->u.seqcutoff.promotion_count >= 0) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_promotion_count(mngt_ctx->core,
									       mngt_ctx->u.seqcutoff.promotion_count))) {
				SPDK_ERRLOG("OCF core '%s': failed to set sequential cut-off promotion_count (OCF error: %d)\n",
					    ocf_core_get_name(mngt_ctx->core), rc);
				goto err_param;
			}
		}

		if (mngt_ctx->u.seqcutoff.promote_on_threshold >= 0) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_promote_on_threshold(mngt_ctx->core,
										    mngt_ctx->u.seqcutoff.promote_on_threshold))) {
				SPDK_ERRLOG("OCF core '%s': failed to set sequential cut-off promote_on_threshold (OCF error: %d)\n",
					    ocf_core_get_name(mngt_ctx->core), rc);
				goto err_param;
			}
		}
	} else {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': setting sequential cut-off on all cores in cache device\n",
			      mngt_ctx->bdev_name);

		if (mngt_ctx->u.seqcutoff.policy >= ocf_seq_cutoff_policy_always &&
		    mngt_ctx->u.seqcutoff.policy < ocf_seq_cutoff_policy_max) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_policy_all(cache,
									  mngt_ctx->u.seqcutoff.policy))) {
				SPDK_ERRLOG("OCF cache '%s': failed to set sequential cut-off policy (OCF error: %d)\n",
					    ocf_cache_get_name(cache), rc);
				goto err_param;
			}
		}

		if (mngt_ctx->u.seqcutoff.threshold >= 0) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_threshold_all(cache,
									     mngt_ctx->u.seqcutoff.threshold * KiB))) {
				SPDK_ERRLOG("OCF cache '%s': failed to set sequential cut-off threshold (OCF error: %d)\n",
					    ocf_cache_get_name(cache), rc);
				goto err_param;
			}
		}

		if (mngt_ctx->u.seqcutoff.promotion_count >= 0) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_promotion_count_all(cache,
										   mngt_ctx->u.seqcutoff.promotion_count))) {
				SPDK_ERRLOG("OCF cache '%s': failed to set sequential cut-off promotion_count (OCF error: %d)\n",
					    ocf_cache_get_name(cache), rc);
				goto err_param;
			}
		}

		if (mngt_ctx->u.seqcutoff.promote_on_threshold >= 0) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_promote_on_threshold_all(cache,
											mngt_ctx->u.seqcutoff.promote_on_threshold))) {
				SPDK_ERRLOG("OCF cache '%s': failed to set sequential cut-off promote_on_threshold (OCF error: %d)\n",
					    ocf_cache_get_name(cache), rc);
				goto err_param;
			}
		}
	}

	/* For compatibility with global _cache_save_cb(). */
	ocf_mngt_cache_get(cache);

	ocf_mngt_cache_save(cache, _cache_save_cb, mngt_ctx);

	return;

err_param:
	ocf_mngt_cache_unlock(cache);
err_lock:
	mngt_ctx->rpc_cb_fn(mngt_ctx->bdev_name, mngt_ctx->rpc_cb_arg, rc);
	free(mngt_ctx);
}

static int
_seqcutoff_cache_visitor(ocf_cache_t cache, void *ctx)
{
	ocf_core_t core;
	struct vbdev_ocf_mngt_ctx *mngt_ctx = ctx;
	int rc;

	if (!strcmp(mngt_ctx->bdev_name, ocf_cache_get_name(cache))) {
		ocf_mngt_cache_lock(cache, _seqcutoff_lock_cb, mngt_ctx);
		return -EEXIST;
	}

	rc = ocf_core_get_by_name(cache, mngt_ctx->bdev_name, OCF_CORE_NAME_SIZE, &core);
	if (!rc) {
		mngt_ctx->core = core;
		ocf_mngt_cache_lock(cache, _seqcutoff_lock_cb, mngt_ctx);
		return -EEXIST;
	}

	return rc == -OCF_ERR_CORE_NOT_EXIST ? 0 : rc;
}

/* RPC entry point. */
void
vbdev_ocf_set_seqcutoff(const char *bdev_name, const char *policy, int32_t threshold,
		       int32_t promotion_count, int32_t promote_on_threshold,
		       vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': setting sequential cut-off params\n", bdev_name);

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF '%s': failed to allocate memory for sequential cut-off set context\n",
			    bdev_name);
		rpc_cb_fn(bdev_name, rpc_cb_arg, -ENOMEM);
		return;
	}
	mngt_ctx->bdev_name = bdev_name;
	/* Will be used later if given bdev is a core device. */
	mngt_ctx->core = NULL;
	mngt_ctx->u.seqcutoff.policy = vbdev_ocf_seqcutoff_policy_get_by_name(policy);
	mngt_ctx->u.seqcutoff.threshold = threshold;
	mngt_ctx->u.seqcutoff.promotion_count = promotion_count;
	mngt_ctx->u.seqcutoff.promote_on_threshold = promote_on_threshold;
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _seqcutoff_cache_visitor, mngt_ctx);
	if (rc && rc != -EEXIST) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
		rpc_cb_fn(bdev_name, rpc_cb_arg, rc);
	} else if (!rc) {
		SPDK_ERRLOG("OCF '%s': not exist\n", bdev_name);
		rpc_cb_fn(bdev_name, rpc_cb_arg, -ENXIO);
	}
}

static void
_flush_cache_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing flush operation\n",
		      ocf_cache_get_name(cache));

	ocf_mngt_cache_read_unlock(cache);

	cache_ctx->flush.error = error;
	cache_ctx->flush.in_progress = false;
}

static void
_flush_core_cb(ocf_core_t core, void *cb_arg, int error)
{
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing flush operation\n",
		      ocf_core_get_name(core));

	ocf_mngt_cache_read_unlock(ocf_core_get_cache(core));

	core_ctx->flush.error = error;
	core_ctx->flush.in_progress = false;
}

static void
_flush_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *flush_ctx = cb_arg;
	struct vbdev_ocf_cache *cache_ctx;
	struct vbdev_ocf_core *core_ctx;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto end;
	}

	if (flush_ctx->core) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': flushing core device\n", flush_ctx->bdev_name);

		core_ctx = ocf_core_get_priv(flush_ctx->core);
		core_ctx->flush.in_progress = true;
		ocf_mngt_core_flush(flush_ctx->core, _flush_core_cb, NULL);
	} else {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': flushing cache device\n", flush_ctx->bdev_name);

		cache_ctx = ocf_cache_get_priv(cache);
		cache_ctx->flush.in_progress = true;
		ocf_mngt_cache_flush(cache, _flush_cache_cb, NULL);
	}

end:
	/* Flushing process may take some time to finish, so call
	 * RPC callback now and leave flush running in background. */
	flush_ctx->rpc_cb_fn(flush_ctx->bdev_name, flush_ctx->rpc_cb_arg, rc);
	free(flush_ctx);
}

static int
_flush_cache_visitor(ocf_cache_t cache, void *ctx)
{
	ocf_core_t core;
	struct vbdev_ocf_mngt_ctx *flush_ctx = ctx;
	int rc;

	if (!strcmp(flush_ctx->bdev_name, ocf_cache_get_name(cache))) {
		ocf_mngt_cache_read_lock(cache, _flush_lock_cb, flush_ctx);
		return -EEXIST;
	}

	rc = ocf_core_get_by_name(cache, flush_ctx->bdev_name, OCF_CORE_NAME_SIZE, &core);
	if (!rc) {
		flush_ctx->core = core;
		ocf_mngt_cache_read_lock(cache, _flush_lock_cb, flush_ctx);
		return -EEXIST;
	}

	return rc == -OCF_ERR_CORE_NOT_EXIST ? 0 : rc;
}

/* RPC entry point. */
void
vbdev_ocf_flush_start(const char *bdev_name, vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	struct vbdev_ocf_mngt_ctx *flush_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': initiating flush operation\n", bdev_name);

	flush_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!flush_ctx) {
		SPDK_ERRLOG("OCF '%s': failed to allocate memory for flush context\n", bdev_name);
		rpc_cb_fn(bdev_name, rpc_cb_arg, -ENOMEM);
		return;
	}
	flush_ctx->bdev_name = bdev_name;
	/* Will be used later if given bdev is a core device. */
	flush_ctx->core = NULL;
	flush_ctx->rpc_cb_fn = rpc_cb_fn;
	flush_ctx->rpc_cb_arg = rpc_cb_arg;

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _flush_cache_visitor, flush_ctx);
	if (rc && rc != -EEXIST) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
		rpc_cb_fn(bdev_name, rpc_cb_arg, rc);
	} else if (!rc) {
		SPDK_ERRLOG("OCF '%s': not exist\n", bdev_name);
		rpc_cb_fn(bdev_name, rpc_cb_arg, -ENXIO);
	}
}

static int
_dump_promotion_info(struct spdk_json_write_ctx *w, ocf_cache_t cache)
{
	ocf_promotion_t promotion_policy;
	uint32_t param_val;
	int rc;

	if ((rc = ocf_mngt_cache_promotion_get_policy(cache, &promotion_policy))) {
		SPDK_ERRLOG("OCF cache '%s': failed to get promotion policy (OCF error: %d)\n",
			    ocf_cache_get_name(cache), rc);
		spdk_json_write_named_string(w, "policy", "");
		return rc;
	}
	spdk_json_write_named_string(w, "policy",
				     vbdev_ocf_promotion_policy_get_name(promotion_policy));

	if (promotion_policy == ocf_promotion_nhit) {
		if ((rc = ocf_mngt_cache_promotion_get_param(cache, ocf_promotion_nhit,
							     ocf_nhit_insertion_threshold, &param_val))) {
			return rc;
		}
		spdk_json_write_named_uint32(w, "insertion_threshold", param_val);

		if ((rc = ocf_mngt_cache_promotion_get_param(cache, ocf_promotion_nhit,
							     ocf_nhit_trigger_threshold, &param_val))) {
			return rc;
		}
		spdk_json_write_named_uint32(w, "trigger_threshold", param_val);

	}

	return 0;
}

static int
_dump_cleaning_info(struct spdk_json_write_ctx *w, ocf_cache_t cache)
{
	ocf_cleaning_t cleaning_policy;
	uint32_t param_val;
	int rc;

	if ((rc = ocf_mngt_cache_cleaning_get_policy(cache, &cleaning_policy))) {
		SPDK_ERRLOG("OCF cache '%s': failed to get cleaning policy (OCF error: %d)\n",
			    ocf_cache_get_name(cache), rc);
		spdk_json_write_named_string(w, "policy", "");
		return rc;
	}
	spdk_json_write_named_string(w, "policy",
				     vbdev_ocf_cleaning_policy_get_name(cleaning_policy));

	if (cleaning_policy == ocf_cleaning_acp) {
		if ((rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_acp,
							    ocf_acp_wake_up_time, &param_val))) {
			return rc;
		}
		spdk_json_write_named_uint32(w, "wake_up_time", param_val);

		if ((rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_acp,
							    ocf_acp_flush_max_buffers, &param_val))) {
			return rc;
		}
		spdk_json_write_named_uint32(w, "flush_max_buffers", param_val);

	} else if (cleaning_policy == ocf_cleaning_alru) {
		if ((rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_alru,
							    ocf_alru_wake_up_time, &param_val))) {
			return rc;
		}
		spdk_json_write_named_uint32(w, "wake_up_time", param_val);

		if ((rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_alru,
							    ocf_alru_flush_max_buffers, &param_val))) {
			return rc;
		}
		spdk_json_write_named_uint32(w, "flush_max_buffers", param_val);

		if ((rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_alru,
							    ocf_alru_stale_buffer_time, &param_val))) {
			return rc;
		}
		spdk_json_write_named_uint32(w, "staleness_time", param_val);

		if ((rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_alru,
							    ocf_alru_activity_threshold, &param_val))) {
			return rc;
		}
		spdk_json_write_named_uint32(w, "activity_threshold", param_val);

		if ((rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_alru,
							    ocf_alru_max_dirty_ratio, &param_val))) {
			return rc;
		}
		spdk_json_write_named_uint32(w, "max_dirty_ratio", param_val);
	}

	return 0;
}

static int
_dump_seqcutoff_info(struct spdk_json_write_ctx *w, ocf_core_t core)
{
	ocf_seq_cutoff_policy seqcutoff_policy;
	uint32_t param_val_int;
	bool param_val_bool;
	int rc;

	if ((rc = ocf_mngt_core_get_seq_cutoff_policy(core, &seqcutoff_policy))) {
		SPDK_ERRLOG("OCF core '%s': failed to get sequential cut-off policy (OCF error: %d)\n",
			    ocf_core_get_name(core), rc);
		spdk_json_write_named_string(w, "policy", "");
		return rc;
	}
	spdk_json_write_named_string(w, "policy",
				     vbdev_ocf_seqcutoff_policy_get_name(seqcutoff_policy));

	if ((rc = ocf_mngt_core_get_seq_cutoff_threshold(core, &param_val_int))) {
		return rc;
	}
	spdk_json_write_named_uint32(w, "threshold", param_val_int / KiB);

	if ((rc = ocf_mngt_core_get_seq_cutoff_promotion_count(core, &param_val_int))) {
		return rc;
	}
	spdk_json_write_named_uint32(w, "promotion_count", param_val_int);

	if ((rc = ocf_mngt_core_get_seq_cutoff_promote_on_threshold(core, &param_val_bool))) {
		return rc;
	}
	spdk_json_write_named_bool(w, "promote_on_threshold", param_val_bool);

	return 0;
}

// handle errors ?
static int
_get_bdevs_core_visit(ocf_core_t core, void *cb_arg)
{
	struct spdk_json_write_ctx *w = cb_arg;
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	int rc;

	spdk_json_write_object_begin(w);
	spdk_json_write_named_string(w, "name", ocf_core_get_name(core));
	spdk_json_write_named_string(w, "base_name", core_ctx ? core_ctx->base.name : "");
	spdk_json_write_named_bool(w, "base_attached",
				   core_ctx ? vbdev_ocf_core_is_base_attached(core_ctx) : false);
	spdk_json_write_named_bool(w, "loading", !core_ctx);

	spdk_json_write_named_object_begin(w, "seq_cutoff");
	if ((rc = _dump_seqcutoff_info(w, core))) {
		SPDK_ERRLOG("OCF core '%s': failed to get sequential cut-off params info (OCF error: %d)\n",
			    ocf_core_get_name(core), rc);
	}
	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "flush");
	spdk_json_write_named_bool(w, "in_progress", core_ctx ? core_ctx->flush.in_progress : false);
	spdk_json_write_named_int32(w, "error", core_ctx ? core_ctx->flush.error : 0);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	return 0;
}

// handle errors ?
static int
_get_bdevs_cache_visit(ocf_cache_t cache, void *cb_arg)
{
	struct spdk_json_write_ctx *w = cb_arg;
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	int rc;

	spdk_json_write_object_begin(w);
	spdk_json_write_named_string(w, "name", ocf_cache_get_name(cache));
	spdk_json_write_named_string(w, "base_name", cache_ctx->base.name);
	spdk_json_write_named_bool(w, "base_attached", ocf_cache_is_device_attached(cache));
	spdk_json_write_named_string(w, "cache_mode",
				     vbdev_ocf_cachemode_get_name(ocf_cache_get_mode(cache)));
	spdk_json_write_named_uint32(w, "cache_line_size",
				     ocf_cache_get_line_size(cache) / KiB);

	spdk_json_write_named_object_begin(w, "promotion");
	if ((rc = _dump_promotion_info(w, cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to get promotion params info (OCF error: %d)\n",
			    ocf_cache_get_name(cache), rc);
	}
	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "cleaning");
	if ((rc = _dump_cleaning_info(w, cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to get cleaning params info (OCF error: %d)\n",
			    ocf_cache_get_name(cache), rc);
	}
	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "flush");
	spdk_json_write_named_bool(w, "in_progress", cache_ctx->flush.in_progress);
	spdk_json_write_named_int32(w, "error", cache_ctx->flush.error);
	spdk_json_write_object_end(w);

	spdk_json_write_named_uint16(w, "cores_count", ocf_cache_get_core_count(cache));
	spdk_json_write_named_array_begin(w, "cores");
	rc = ocf_core_visit(cache, _get_bdevs_core_visit, w, false);
	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);

	return rc;
}

/* RPC entry point. */
void
vbdev_ocf_get_bdevs(const char *name, vbdev_ocf_get_bdevs_cb rpc_cb_fn, void *rpc_cb_arg1, void *rpc_cb_arg2)
{
	struct spdk_json_write_ctx *w = rpc_cb_arg1;
	struct vbdev_ocf_core *core_ctx;
	int rc;

	// TODO: dump info about 'name' bdev only

	spdk_json_write_named_array_begin(w, "cores_waitlist");
	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		spdk_json_write_object_begin(w);
		spdk_json_write_named_string(w, "name", vbdev_ocf_core_get_name(core_ctx));
		spdk_json_write_named_string(w, "cache_name", core_ctx->cache_name);
		spdk_json_write_named_string(w, "base_name", core_ctx->base.name);
		spdk_json_write_named_bool(w, "base_attached", vbdev_ocf_core_is_base_attached(core_ctx));
		spdk_json_write_object_end(w);
	}
	spdk_json_write_array_end(w);

	spdk_json_write_named_array_begin(w, "caches");
	if ((rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _get_bdevs_cache_visit, w))) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
	}
	spdk_json_write_array_end(w);

	rpc_cb_fn(rpc_cb_arg1, rpc_cb_arg2);
}

SPDK_LOG_REGISTER_COMPONENT(vbdev_ocf)
