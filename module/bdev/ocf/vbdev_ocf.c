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
#include "stats.h"
#include "utils.h"
#include "volume.h"

/* This namespace UUID was generated using uuid_generate() method. */
#define BDEV_OCF_NAMESPACE_UUID "f92b7f49-f6c0-44c8-bd23-3205e8c3b6ad"
bool g_vbdev_ocf_module_is_running = false;

static int vbdev_ocf_module_init(void);
static void vbdev_ocf_module_fini_start(void);
static void vbdev_ocf_module_fini(void);
static int vbdev_ocf_module_get_ctx_size(void);
static void vbdev_ocf_module_examine_config(struct spdk_bdev *bdev);
static void vbdev_ocf_module_examine_disk(struct spdk_bdev *bdev);
static int vbdev_ocf_module_config_json(struct spdk_json_write_ctx *w);

struct spdk_bdev_module ocf_if = {
	.name = "OCF",
	.module_init = vbdev_ocf_module_init,
	.fini_start = vbdev_ocf_module_fini_start,
	.async_fini_start = true,
	.module_fini = vbdev_ocf_module_fini,
	.get_ctx_size = vbdev_ocf_module_get_ctx_size,
	.examine_config = vbdev_ocf_module_examine_config,
	.examine_disk = vbdev_ocf_module_examine_disk,
	.config_json = vbdev_ocf_module_config_json,
};
SPDK_BDEV_MODULE_REGISTER(ocf, &ocf_if)

static int vbdev_ocf_fn_destruct(void *ctx);
static void vbdev_ocf_fn_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io);
static bool vbdev_ocf_fn_io_type_supported(void *ctx, enum spdk_bdev_io_type);
static struct spdk_io_channel *vbdev_ocf_fn_get_io_channel(void *ctx);
static int vbdev_ocf_fn_dump_info_json(void *ctx, struct spdk_json_write_ctx *w);
static void vbdev_ocf_fn_dump_device_stat_json(void *ctx, struct spdk_json_write_ctx *w);
static void vbdev_ocf_fn_reset_device_stat(void *ctx);

struct spdk_bdev_fn_table vbdev_ocf_fn_table = {
	.destruct = vbdev_ocf_fn_destruct,
	.submit_request = vbdev_ocf_fn_submit_request,
	.io_type_supported = vbdev_ocf_fn_io_type_supported,
	.get_io_channel = vbdev_ocf_fn_get_io_channel,
	.dump_info_json = vbdev_ocf_fn_dump_info_json,
	.dump_device_stat_json = vbdev_ocf_fn_dump_device_stat_json,
	.reset_device_stat = vbdev_ocf_fn_reset_device_stat,
};

static int
_bdev_exists_cache_visitor(ocf_cache_t cache, void *ctx)
{
	char *name = ctx;
	ocf_core_t core;
	int rc;

	rc = ocf_core_get_by_name(cache, name, OCF_CORE_NAME_SIZE, &core);
	/* Check if found core has context (priv) attached as well. Only then
	 * it counts as a regular core and not just added during cache load. */
	if (!rc && ocf_core_get_priv(core)) {
		return -EEXIST;
	} else if (rc && rc != -OCF_ERR_CORE_NOT_EXIST) {
		SPDK_ERRLOG("OCF: failed to get core: %s\n", spdk_strerror(-rc));
	}

	return 0;
}

static bool
vbdev_ocf_bdev_exists(const char *name)
{
	ocf_cache_t cache;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': looking for it in existing bdev names\n", name);

	if (vbdev_ocf_core_waitlist_get_by_name(name)) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': found in core wait list\n", name);

		return true;
	}

	rc = ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, name, OCF_CACHE_NAME_SIZE, &cache);
	if (!rc) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': found cache\n", name);

		/* If cache was found, do not increase its refcount. */
		ocf_mngt_cache_put(cache);

		return true;
	} else if (rc && rc != -OCF_ERR_CACHE_NOT_EXIST) {
		SPDK_ERRLOG("OCF: failed to get cache: %s\n", spdk_strerror(-rc));
	}

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _bdev_exists_cache_visitor, (char *)name);
	if (rc == -EEXIST) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': found core\n", name);

		return true;
	} else if (rc) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
	}

	if (spdk_bdev_get_by_name(name)) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': found in SPDK bdev layer\n", name);

		return true;
	}

	return false;
}

static int
_bdev_resolve_cache_visitor(ocf_cache_t cache, void *ctx)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = ctx;
	int rc;

	rc = ocf_core_get_by_name(cache, mngt_ctx->bdev_name, OCF_CORE_NAME_SIZE, &mngt_ctx->core);
	if (rc && rc != -OCF_ERR_CORE_NOT_EXIST) {
		return rc;
	} else if (!rc) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': found core\n", mngt_ctx->bdev_name);

		return -EEXIST;
	}

	return 0;
}

/* Takes name of bdev and saves pointer to either cache or
 * core of that name inside given management context. */
static int
vbdev_ocf_bdev_resolve(struct vbdev_ocf_mngt_ctx *mngt_ctx)
{
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': looking for cache or core of that name\n",
		      mngt_ctx->bdev_name);

	rc = ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, mngt_ctx->bdev_name, OCF_CACHE_NAME_SIZE,
					&mngt_ctx->cache);
	if (rc && rc != -OCF_ERR_CACHE_NOT_EXIST) {
		return rc;
	} else if (!rc) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': found cache\n", mngt_ctx->bdev_name);

		/* If cache was found, do not increase its refcount. */
		ocf_mngt_cache_put(mngt_ctx->cache);
		return 0;
	}

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _bdev_resolve_cache_visitor, mngt_ctx);
	assert(!(mngt_ctx->cache && mngt_ctx->core));
	if (rc && rc != -EEXIST) {
		return rc;
	} else if (!rc || (!mngt_ctx->cache && !mngt_ctx->core)) {
		return -ENXIO;
	}

	return 0;
}

static void
vbdev_ocf_mem_calculate(ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	uint64_t mem_needed, volume_size;

	volume_size = spdk_bdev_get_block_size(cache_ctx->base.bdev) *
		      spdk_bdev_get_num_blocks(cache_ctx->base.bdev);
	mem_needed = ocf_mngt_get_ram_needed(cache, volume_size);

	SPDK_NOTICELOG("Needed memory to start cache in this configuration "
		       "(device size: %"PRIu64", cache line size: %"PRIu64"): %"PRIu64"\n",
		       volume_size, cache_ctx->cache_cfg.cache_line_size, mem_needed);
}

static int
vbdev_ocf_module_init(void)
{
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF: starting module\n");

	if ((rc = vbdev_ocf_ctx_init())) {
		SPDK_ERRLOG("OCF: failed to initialize context: %s\n", spdk_strerror(-rc));
		return rc;
	}

	if ((rc = vbdev_ocf_volume_init())) {
		vbdev_ocf_ctx_cleanup();
		SPDK_ERRLOG("OCF: failed to register volume: %s\n", spdk_strerror(-rc));
		return rc;
	}

	g_vbdev_ocf_module_is_running = true;

	return rc;
}

static void
_cache_stop_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing stop of OCF cache\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to properly stop OCF cache (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': stopped\n", ocf_cache_get_name(cache));
	}

	/* In module fini (no management context) do the cleanup despite the error. */
	if (!error || !mngt_ctx) {
		if (vbdev_ocf_cache_is_base_attached(cache)) {
			vbdev_ocf_cache_base_detach(cache);
		}
		vbdev_ocf_cache_mngt_queue_put(cache);
		vbdev_ocf_cache_destroy(cache);
	}

	ocf_mngt_cache_unlock(cache);

	if (mngt_ctx) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing stop\n",
			      ocf_cache_get_name(cache));

		mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, error);
		free(mngt_ctx);
	} else if (!ocf_mngt_cache_get_count(vbdev_ocf_ctx)) {
		/* In module fini (no management context) call spdk_bdev_module_fini_start_done()
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
	struct vbdev_ocf_mngt_ctx *mngt_ctx = core_ctx->mngt_ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing unregister of OCF vbdev\n",
		      ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to unregister OCF vbdev during cache stop: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-error));
	}

	vbdev_ocf_core_destroy(core_ctx);

	if (ocf_cache_get_core_count(cache) == ocf_cache_get_core_inactive_count(cache)) {
		/* All cores in this cache were already unregistered
		 * and detached, so proceed with stopping the cache. */
		ocf_mngt_cache_lock(cache, _cache_stop_lock_cb, mngt_ctx);
	}
}

static int
_cache_stop_core_visitor(ocf_core_t core, void *cb_arg)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': cache stop visit\n", ocf_core_get_name(core));

	if (!core_ctx) {
		/* Skip this core. If there is no context, it means that this core
		 * was added from metadata during cache load and it's just an empty shell. */
		return 0;
	}

	/* If core is detached it's already unregistered, so just free its data and exit. */
	if (!vbdev_ocf_core_is_base_attached(core_ctx)) {
		vbdev_ocf_core_destroy(core_ctx);
		return 0;
	}

	core_ctx->mngt_ctx = mngt_ctx;

	if ((rc = vbdev_ocf_core_unregister(core_ctx, _cache_stop_core_unregister_cb, core))) {
		SPDK_ERRLOG("OCF core '%s': failed to start unregistering OCF vbdev: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-rc));
		return rc;
	}

	return rc;
}

static int
_module_fini_cache_visitor(ocf_cache_t cache, void *cb_arg)
{
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': module stop visit\n", ocf_cache_get_name(cache));

	if (!ocf_cache_get_core_count(cache) ||
	    ocf_cache_get_core_count(cache) == ocf_cache_get_core_inactive_count(cache)) {
		/* If there are no cores or all of them are detached,
		 * then cache stop can be triggered already. */
		ocf_mngt_cache_lock(cache, _cache_stop_lock_cb, NULL);
	}

	if ((rc = ocf_core_visit(cache, _cache_stop_core_visitor, NULL, false))) {
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

	g_vbdev_ocf_module_is_running = false;

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		if (vbdev_ocf_core_is_base_attached(core_ctx)) {
			vbdev_ocf_core_base_detach(core_ctx);
		}
	}

	if (!ocf_mngt_cache_get_count(vbdev_ocf_ctx)) {
		spdk_bdev_module_fini_start_done();
		return;
	}

	if ((rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _module_fini_cache_visitor, NULL))) {
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
		vbdev_ocf_core_waitlist_remove(core_ctx);
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
_examine_config_core_visitor(ocf_core_t core, void *cb_arg)
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

	if (!strcmp(spdk_bdev_get_product_name(spdk_bdev_get_by_name(bdev_name)), "OCF_disk")) {
		SPDK_ERRLOG("OCF core '%s': base bdev '%s' is already an OCF core\n",
			    ocf_core_get_name(core), bdev_name);
		return -ENOTSUP;
	}

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
_examine_config_cache_visitor(ocf_cache_t cache, void *cb_arg)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	char *bdev_name = cb_arg;
	int rc = 0;

	if (strcmp(bdev_name, cache_ctx->base.name)) {
		return ocf_core_visit(cache, _examine_config_core_visitor, bdev_name, false);
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

	SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': looking for vbdevs waiting for it\n", bdev_name);

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		if (strcmp(bdev_name, core_ctx->base.name)) {
			continue;
		}

		SPDK_NOTICELOG("OCF core '%s': base bdev '%s' found\n",
			       vbdev_ocf_core_get_name(core_ctx), bdev_name);

		if (!strcmp(spdk_bdev_get_product_name(bdev), "OCF_disk")) {
			SPDK_ERRLOG("OCF core '%s': base bdev '%s' is already an OCF core\n",
				    vbdev_ocf_core_get_name(core_ctx), bdev_name);
			spdk_bdev_module_examine_done(&ocf_if);
			return;
		}

		assert(!vbdev_ocf_core_is_base_attached(core_ctx));

		if ((rc = vbdev_ocf_core_base_attach(core_ctx, bdev_name))) {
			SPDK_ERRLOG("OCF core '%s': failed to attach base bdev '%s': %s\n",
				    vbdev_ocf_core_get_name(core_ctx), bdev_name, spdk_strerror(-rc));
			spdk_bdev_module_examine_done(&ocf_if);
			return;
		}

		spdk_bdev_module_examine_done(&ocf_if);
		return;
	}

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _examine_config_cache_visitor, bdev_name);
	if (rc && rc != -EEXIST) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
	}

	spdk_bdev_module_examine_done(&ocf_if);
}

static void
_core_add_examine_err_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_core *core_ctx = cb_arg;
	ocf_cache_t cache;
	int rc;

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to remove OCF core device (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), error);
	}

	if ((rc = ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, core_ctx->cache_name,
					     OCF_CACHE_NAME_SIZE, &cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to find cache of that name (OCF error: %d)\n",
			    core_ctx->cache_name, rc);
		assert(false);
	}
	ocf_mngt_cache_put(cache);

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	vbdev_ocf_core_base_detach(core_ctx);
	spdk_bdev_module_examine_done(&ocf_if);
}

static void
_core_add_examine_add_cb(ocf_cache_t cache, ocf_core_t core, void *cb_arg, int error)
{
	struct vbdev_ocf_core *core_ctx = cb_arg;
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
		SPDK_ERRLOG("OCF core '%s': failed to register vbdev: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-rc));
		ocf_mngt_cache_remove_core(core, _core_add_examine_err_cb, core_ctx);
		return;
	}

	SPDK_NOTICELOG("OCF core '%s': added to cache '%s'\n",
		       ocf_core_get_name(core), ocf_cache_get_name(cache));

	/* If core was taken from waitlist, remove it from there. */
	if (vbdev_ocf_core_waitlist_get_by_name(vbdev_ocf_core_get_name(core_ctx))) {
		vbdev_ocf_core_waitlist_remove(core_ctx);
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
_cache_attach_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing device attach\n",
		      ocf_cache_get_name(cache));

	/* At this point volume was either moved to ocf_cache_t struct or is no longer
	 * needed due to some errors, so we need to deallocate it either way. */
	vbdev_ocf_cache_config_volume_destroy(cache);
	ocf_mngt_cache_unlock(cache);

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach OCF cache device (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);

		if (error == -OCF_ERR_NO_MEM) {
			SPDK_ERRLOG("Not enough memory to handle cache device of this size. Try to increase hugepage memory size, increase cache line size or use smaller cache device.\n");
			vbdev_ocf_mem_calculate(cache);
		}

		vbdev_ocf_cache_base_detach(cache);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': device attached\n", ocf_cache_get_name(cache));

		/* Update cache IO channel after new device attach. */
		if ((error = vbdev_ocf_core_create_cache_channel(cache))) {
			SPDK_ERRLOG("OCF cache '%s': failed to create channel for newly attached cache: %s\n",
				    ocf_cache_get_name(cache), spdk_strerror(-error));
		} else {
			vbdev_ocf_core_add_from_waitlist(cache);
		}
	}

	if (mngt_ctx->rpc_cb_fn) {
		mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, error);
	} else {
		spdk_bdev_module_examine_done(&ocf_if);
	}
	free(mngt_ctx);
}

static void
_cache_attach_examine_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': attaching OCF cache device\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto err_lock;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for examine attach context: %s\n",
			    ocf_cache_get_name(cache), spdk_strerror(-ENOMEM));
		goto err_alloc;
	}
	mngt_ctx->cache = cache;
	mngt_ctx->u.att_cb_fn = _cache_attach_cb;

	if ((rc = vbdev_ocf_cache_volume_attach(cache, mngt_ctx))) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach volume: %s\n",
			    ocf_cache_get_name(cache), spdk_strerror(-rc));
		goto err_attach;
	}

	return;

err_attach:
	free(mngt_ctx);
err_alloc:
	ocf_mngt_cache_unlock(cache);
err_lock:
	vbdev_ocf_cache_config_volume_destroy(cache);
	vbdev_ocf_cache_base_detach(cache);
	spdk_bdev_module_examine_done(&ocf_if);
}

static int
_examine_disk_core_visitor(ocf_core_t core, void *cb_arg)
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
_examine_disk_cache_visitor(ocf_cache_t cache, void *cb_arg)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	char *bdev_name = cb_arg;

	if (strcmp(bdev_name, cache_ctx->base.name)) {
		return ocf_core_visit(cache, _examine_disk_core_visitor, bdev_name, false);
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
			ocf_mngt_cache_put(cache);
			spdk_bdev_module_examine_done(&ocf_if);
			return;
		}

		core_ctx->core_cfg.try_add = vbdev_ocf_core_is_loaded(vbdev_ocf_core_get_name(core_ctx));

		ocf_mngt_cache_lock(cache, _core_add_examine_lock_cb, core_ctx);
		return;
	}

	rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _examine_disk_cache_visitor, bdev_name);
	if (rc && rc != -EEXIST) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
	} else if (!rc) {
		/* No visitor matched this new bdev, so no one called _examine_done(). */
		spdk_bdev_module_examine_done(&ocf_if);
	}
}

static void
dump_core_config(struct spdk_json_write_ctx *w, struct vbdev_ocf_core *core_ctx)
{
	spdk_json_write_object_begin(w);
	spdk_json_write_named_string(w, "method", "bdev_ocf_add_core");

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "core_name", vbdev_ocf_core_get_name(core_ctx));
	spdk_json_write_named_string(w, "base_name", core_ctx->base.name);
	spdk_json_write_named_string(w, "cache_name", core_ctx->cache_name);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);
}

static void
dump_cache_config(struct spdk_json_write_ctx *w, ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	spdk_json_write_object_begin(w);
	spdk_json_write_named_string(w, "method", "bdev_ocf_start_cache");

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "cache_name", ocf_cache_get_name(cache));
	spdk_json_write_named_string(w, "base_name", cache_ctx->base.name);
	spdk_json_write_named_string(w, "cache_mode",
				     vbdev_ocf_cachemode_get_name(ocf_cache_get_mode(cache)));
	spdk_json_write_named_uint32(w, "cache_line_size", ocf_cache_get_line_size(cache));
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);
}

static int
_module_config_json_core_visitor(ocf_core_t core, void *ctx)
{
	struct spdk_json_write_ctx *w = ctx;
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': module config visit\n", ocf_core_get_name(core));

	if (!core_ctx) {
		/* Skip this core. If there is no context, it means that this core
		 * was added from metadata during cache load and not manually by RPC call. */
		return 0;
	}

	dump_core_config(w, core_ctx);

	return 0;
}

static int
_module_config_json_cache_visitor(ocf_cache_t cache, void *ctx)
{
	struct spdk_json_write_ctx *w = ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': module config visit\n", ocf_cache_get_name(cache));

	dump_cache_config(w, cache);

	return ocf_core_visit(cache, _module_config_json_core_visitor, w, false);
}

static int
vbdev_ocf_module_config_json(struct spdk_json_write_ctx *w)
{
	struct vbdev_ocf_core *core_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF: generating current module configuration\n");

	vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
		dump_core_config(w, core_ctx);
	}

	if ((rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _module_config_json_cache_visitor, w))) {
		SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
		return rc;
	}

	return 0;
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
	}

	/* Detach core instead of removing it, so it stays in the cache metadata. */
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
	}

	if (ocf_mngt_core_is_dirty(core)) {
		ocf_mngt_core_flush(core, _destruct_core_flush_cb, cache);
	} else {
		/* Detach core instead of removing it, so it stays in the cache metadata. */
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

	ocf_io_put(io);

	if (error == -OCF_ERR_NO_MEM) {
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_NOMEM);
	} else if (error) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to complete OCF IO: %s\n",
			    spdk_bdev_get_name(bdev_io->bdev), spdk_strerror(-error));
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

	/* OCF core should be added before vbdev register and removed after vbdev unregister. */
	assert(core);

	io = ocf_volume_new_io(ocf_core_get_front_volume(core), ch_ctx->queue,
			       offset, len, dir, 0, flags);
	if (spdk_unlikely(!io)) {
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

	if (spdk_unlikely(!success)) {
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

	switch (bdev_io->type) {
	case SPDK_BDEV_IO_TYPE_READ:
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
vbdev_ocf_fn_get_io_channel(void *ctx)
{
	ocf_core_t core = ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': got request for IO channel\n",
		      spdk_bdev_get_name(&(((struct vbdev_ocf_core *)ocf_core_get_priv(core))->ocf_vbdev)));

	return spdk_get_io_channel(core);
}

static int
vbdev_ocf_fn_dump_info_json(void *ctx, struct spdk_json_write_ctx *w)
{
	ocf_core_t core = ctx;
	ocf_cache_t cache = ocf_core_get_cache(core);
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF vbdev '%s': dumping driver specific info\n", ocf_core_get_name(core));

	spdk_json_write_named_object_begin(w, "ocf");
	spdk_json_write_named_string(w, "name", ocf_core_get_name(core));
	spdk_json_write_named_string(w, "base_name", core_ctx ? core_ctx->base.name : "");

	spdk_json_write_named_object_begin(w, "cache");
	spdk_json_write_named_string(w, "name", ocf_cache_get_name(cache));
	spdk_json_write_named_string(w, "base_name", cache_ctx->base.name);
	spdk_json_write_named_string(w, "cache_mode",
				     vbdev_ocf_cachemode_get_name(ocf_cache_get_mode(cache)));
	spdk_json_write_named_uint32(w, "cache_line_size", ocf_cache_get_line_size(cache));
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	return 0;
}

static void
vbdev_ocf_fn_dump_device_stat_json(void *ctx, struct spdk_json_write_ctx *w)
{
	ocf_core_t core = ctx;
	struct vbdev_ocf_stats stats;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': collecting statistics\n", ocf_core_get_name(core));

	if ((rc = vbdev_ocf_stats_core_get(core, &stats))) {
		SPDK_ERRLOG("OCF core '%s': failed to collect statistics (OCF error: %d)\n",
			    ocf_core_get_name(core), rc);
		return;
	}

	vbdev_ocf_stats_write_json(w, &stats);
}

/* Do not define this function to not reset OCF stats when resetting exposed bdev's stats.
 * Let the user reset OCF stats independently by calling bdev_ocf_reset_stats RPC. */
static void
vbdev_ocf_fn_reset_device_stat(void *ctx)
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
	ocf_mngt_cache_unlock(cache);
}

static void
_cache_start_rpc_attach_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing start\n", ocf_cache_get_name(cache));

	/* At this point volume was either moved to ocf_cache_t struct or is no longer
	 * needed due to some errors, so we need to deallocate it either way. */
	vbdev_ocf_cache_config_volume_destroy(cache);

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach OCF cache device (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);

		if (error == -OCF_ERR_NO_MEM) {
			SPDK_ERRLOG("Not enough memory to handle cache device of this size. Try to increase hugepage memory size, increase cache line size or use smaller cache device.\n");
			vbdev_ocf_mem_calculate(cache);
		}

		vbdev_ocf_cache_base_detach(cache);
		vbdev_ocf_cache_mngt_queue_put(cache);
		ocf_mngt_cache_stop(cache, _cache_start_rpc_err_cb, NULL);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': started\n", ocf_cache_get_name(cache));

		ocf_mngt_cache_unlock(cache);
		vbdev_ocf_core_add_from_waitlist(cache);
	}

	mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, error);
	free(mngt_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_cache_start(const char *cache_name, const char *base_name,
		      const char *cache_mode, const uint32_t cache_line_size, bool no_load,
		      vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating start\n", cache_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

	if (vbdev_ocf_bdev_exists(cache_name)) {
		SPDK_ERRLOG("OCF '%s': bdev already exists\n", cache_name);
		rc = -EEXIST;
		goto err_exist;
	}

	if ((rc = vbdev_ocf_cache_create(&cache, cache_name, cache_mode,
					 cache_line_size, no_load))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create cache: %s\n",
			    cache_name, spdk_strerror(-rc));
		goto err_create;
	}

	if ((rc = vbdev_ocf_cache_mngt_queue_create(cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create management queue: %s\n",
			    cache_name, spdk_strerror(-rc));
		goto err_queue;
	}

	/* Check if base device for this cache is already present. */
	if ((rc = vbdev_ocf_cache_base_attach(cache, base_name))) {
		if (rc == -ENODEV) {
			/* If not, just leave started cache without the device and exit. */
			/* It will be attached later at the examine stage when the device appears. */
			SPDK_NOTICELOG("OCF cache '%s': start deferred - waiting for base bdev '%s'\n",
				       cache_name, base_name);
			ocf_mngt_cache_unlock(cache);
			rpc_cb_fn(cache_name, rpc_cb_arg, -ENODEV);
			return;
		}
		SPDK_ERRLOG("OCF cache '%s': failed to attach base bdev '%s': %s\n",
			    cache_name, base_name, spdk_strerror(-rc));
		goto err_base;
	}

	if ((rc = vbdev_ocf_cache_config_volume_create(cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create config volume: %s\n",
			    cache_name, spdk_strerror(-rc));
		goto err_volume;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache start context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->cache = cache;
	mngt_ctx->u.att_cb_fn = _cache_start_rpc_attach_cb;

	if ((rc = vbdev_ocf_cache_volume_attach(cache, mngt_ctx))) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach volume: %s\n",
			    cache_name, spdk_strerror(-rc));
		goto err_attach;
	}

	return;

err_attach:
	free(mngt_ctx);
err_alloc:
	vbdev_ocf_cache_config_volume_destroy(cache);
err_volume:
	vbdev_ocf_cache_base_detach(cache);
err_base:
	vbdev_ocf_cache_mngt_queue_put(cache);
err_queue:
	ocf_mngt_cache_stop(cache, _cache_start_rpc_err_cb, NULL);
err_create:
err_exist:
err_module:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

/* RPC entry point. */
void
vbdev_ocf_cache_stop(const char *cache_name, vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating stop\n", cache_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache stop context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->cache = cache;

	if (!ocf_cache_get_core_count(cache) ||
	    ocf_cache_get_core_count(cache) == ocf_cache_get_core_inactive_count(cache)) {
		/* If there are no cores or all of them are detached,
		 * then cache stop can be triggered already. */
		ocf_mngt_cache_lock(cache, _cache_stop_lock_cb, mngt_ctx);
	}

	if ((rc = ocf_core_visit(cache, _cache_stop_core_visitor, mngt_ctx, false))) {
		SPDK_ERRLOG("OCF cache '%s': failed to iterate over core bdevs: %s\n",
			    ocf_cache_get_name(cache), spdk_strerror(-rc));
		goto err_visit;
	}

	return;

err_visit:
	free(mngt_ctx);
err_alloc:
	ocf_mngt_cache_put(cache);
err_cache:
err_module:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_cache_detach_rpc_detach_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing device detach\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to detach OCF cache device (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	} else {
		vbdev_ocf_cache_base_detach(cache);

		/* Update cache IO channel after device detach. */
		if ((error = vbdev_ocf_core_destroy_cache_channel(cache))) {
			SPDK_ERRLOG("OCF cache '%s': failed to destroy channel for detached cache: %s\n",
				    ocf_cache_get_name(cache), spdk_strerror(-error));
		}

		SPDK_NOTICELOG("OCF cache '%s': device detached\n", ocf_cache_get_name(cache));
	}

	mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, error);
	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	free(mngt_ctx);
}

static void
_cache_detach_rpc_flush_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to flush OCF cache (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, error);
		ocf_mngt_cache_unlock(cache);
		ocf_mngt_cache_put(cache);
		free(mngt_ctx);
		return;
	}

	ocf_mngt_cache_detach(cache, _cache_detach_rpc_detach_cb, mngt_ctx);
}

static void
_cache_detach_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': detaching OCF cache device\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, error);
		ocf_mngt_cache_put(cache);
		free(mngt_ctx);
		return;
	}

	if (ocf_mngt_cache_is_dirty(cache)) {
		ocf_mngt_cache_flush(cache, _cache_detach_rpc_flush_cb, mngt_ctx);
	} else {
		ocf_mngt_cache_detach(cache, _cache_detach_rpc_detach_cb, mngt_ctx);
	}
}

/* RPC entry point. */
void
vbdev_ocf_cache_detach(const char *cache_name, vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating device detach\n", cache_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	if (!ocf_cache_is_device_attached(cache)) {
		SPDK_ERRLOG("OCF cache '%s': device already detached\n", cache_name);
		rc = -EALREADY;
		goto err_state;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache detach context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;

	ocf_mngt_cache_lock(cache, _cache_detach_rpc_lock_cb, mngt_ctx);

	return;

err_alloc:
err_state:
	ocf_mngt_cache_put(cache);
err_cache:
err_module:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_cache_attach_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': attaching OCF cache device\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, error);
		vbdev_ocf_cache_config_volume_destroy(cache);
		vbdev_ocf_cache_base_detach(cache);
		free(mngt_ctx);
		return;
	}

	ocf_mngt_cache_attach(cache, &cache_ctx->cache_att_cfg, _cache_attach_cb,
			      mngt_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_cache_attach(const char *cache_name, const char *base_name, bool force,
		       vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_cache *cache_ctx;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating device attach\n", cache_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	if (!ocf_cache_is_detached(cache)) {
		SPDK_ERRLOG("OCF cache '%s': device already attached\n", cache_name);
		rc = -EEXIST;
		goto err_state;
	}

	cache_ctx = ocf_cache_get_priv(cache);
	cache_ctx->no_load = force;

	/* Check if base device to attach to this cache is already present. */
	if ((rc = vbdev_ocf_cache_base_attach(cache, base_name))) {
		if (rc == -ENODEV) {
			/* If not, just leave it here and exit. It will be attached
			 * later at the examine stage when the device appears. */
			SPDK_NOTICELOG("OCF cache '%s': attach deferred - waiting for base bdev '%s'\n",
				       cache_name, base_name);
			rpc_cb_fn(cache_name, rpc_cb_arg, -ENODEV);
			return;
		}
		SPDK_ERRLOG("OCF cache '%s': failed to attach base bdev '%s': %s\n",
			    cache_name, base_name, spdk_strerror(-rc));
		goto err_base;
	}

	if ((rc = vbdev_ocf_cache_config_volume_create(cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to create config volume: %s\n",
			    cache_name, spdk_strerror(-rc));
		goto err_volume;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache attach context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;

	ocf_mngt_cache_put(cache);
	ocf_mngt_cache_lock(cache, _cache_attach_rpc_lock_cb, mngt_ctx);

	return;

err_alloc:
	vbdev_ocf_cache_config_volume_destroy(cache);
err_volume:
	vbdev_ocf_cache_base_detach(cache);
err_base:
err_state:
	ocf_mngt_cache_put(cache);
err_cache:
err_module:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_core_add_rpc_err_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = mngt_ctx->u.core_ctx;
	ocf_cache_t cache = mngt_ctx->cache;

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to remove OCF core device (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), error);
	}

	mngt_ctx->rpc_cb_fn(vbdev_ocf_core_get_name(core_ctx), mngt_ctx->rpc_cb_arg, error);
	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	vbdev_ocf_core_base_detach(core_ctx);
	vbdev_ocf_core_destroy(core_ctx);
	free(mngt_ctx);
}

static void
_core_add_rpc_add_cb(ocf_cache_t cache, ocf_core_t core, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = mngt_ctx->u.core_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing add of OCF core\n",
		      vbdev_ocf_core_get_name(core_ctx));
	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing add\n", vbdev_ocf_core_get_name(core_ctx));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to add core to OCF cache '%s' (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), ocf_cache_get_name(cache), error);
		mngt_ctx->rpc_cb_fn(vbdev_ocf_core_get_name(core_ctx), mngt_ctx->rpc_cb_arg, error);
		ocf_mngt_cache_unlock(cache);
		ocf_mngt_cache_put(cache);
		vbdev_ocf_core_base_detach(core_ctx);
		vbdev_ocf_core_destroy(core_ctx);
		free(mngt_ctx);
		return;
	}

	ocf_core_set_priv(core, core_ctx);

	if ((rc = vbdev_ocf_core_register(core))) {
		SPDK_ERRLOG("OCF core '%s': failed to register vbdev: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-rc));
		ocf_mngt_cache_remove_core(core, _core_add_rpc_err_cb, mngt_ctx);
		return;
	}

	SPDK_NOTICELOG("OCF core '%s': added to cache '%s'\n",
		       ocf_core_get_name(core), ocf_cache_get_name(cache));

	mngt_ctx->rpc_cb_fn(ocf_core_get_name(core), mngt_ctx->rpc_cb_arg, rc);
	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	free(mngt_ctx);
}

static void
_core_add_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = mngt_ctx->u.core_ctx;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating add of OCF core\n",
		      vbdev_ocf_core_get_name(core_ctx));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    vbdev_ocf_core_get_name(core_ctx), error);
		mngt_ctx->rpc_cb_fn(vbdev_ocf_core_get_name(core_ctx), mngt_ctx->rpc_cb_arg, error);
		ocf_mngt_cache_put(cache);
		vbdev_ocf_core_base_detach(core_ctx);
		vbdev_ocf_core_destroy(core_ctx);
		free(mngt_ctx);
		return;
	}

	ocf_mngt_cache_add_core(cache, &core_ctx->core_cfg, _core_add_rpc_add_cb, mngt_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_core_add(const char *core_name, const char *base_name, const char *cache_name,
		   vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_cache *cache_ctx;
	struct vbdev_ocf_core *core_ctx;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	uint32_t cache_block_size, core_block_size;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating add\n", core_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

	if (vbdev_ocf_bdev_exists(core_name)) {
		SPDK_ERRLOG("OCF '%s': bdev already exists\n", core_name);
		rc = -EEXIST;
		goto err_exist;
	}

	if ((rc = vbdev_ocf_core_create(&core_ctx, core_name, cache_name))) {
		SPDK_ERRLOG("OCF core '%s': failed to create core: %s\n",
			    core_name, spdk_strerror(-rc));
		goto err_create;
	}

	/* First, check if base device for this core is already present. */
	if ((rc = vbdev_ocf_core_base_attach(core_ctx, base_name))) {
		if (rc == -ENODEV) {
			/* If not, just put core context on the temporary core wait list and exit. */
			/* It will be attached later at the examine stage when the device appears. */
			SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for base bdev '%s'\n",
				       core_name, base_name);
			vbdev_ocf_core_waitlist_add(core_ctx);
			rpc_cb_fn(core_name, rpc_cb_arg, -ENODEV);
			return;
		}
		SPDK_ERRLOG("OCF core '%s': failed to attach base bdev '%s': %s\n",
			    core_name, base_name, spdk_strerror(-rc));
		goto err_no_base;
	}

	if (!strcmp(spdk_bdev_get_product_name(spdk_bdev_get_by_name(base_name)), "OCF_disk")) {
		SPDK_ERRLOG("OCF core '%s': base bdev '%s' is already an OCF core\n", core_name, base_name);
		rc = -ENOTSUP;
		goto err_ocf_base;
	}

	/* Second, check if OCF cache for this core is already started. */
	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		/* If not, just put core context on the temporary core wait list and exit. */
		/* Core will be automatically added later when this cache finally starts. */
		SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for OCF cache '%s'\n",
			       core_name, cache_name);
		vbdev_ocf_core_waitlist_add(core_ctx);
		rpc_cb_fn(core_name, rpc_cb_arg, -ENODEV);
		return;
	}

	cache_ctx = ocf_cache_get_priv(cache);

	/* And finally, check if OCF cache device is already attached.
	 * We need to have cache device attached to know if cache was loaded or attached
	 * and then set 'try_add' in core config accordingly. */
	if (!ocf_cache_is_device_attached(cache) || !vbdev_ocf_cache_is_base_attached(cache)) {
		/* If not, just put core context on the temporary core wait list and exit. */
		/* Core will be automatically added later when device for this cache gets attached. */
		SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for OCF cache device '%s'\n",
			       core_name, cache_ctx->base.name);
		ocf_mngt_cache_put(cache);
		vbdev_ocf_core_waitlist_add(core_ctx);
		rpc_cb_fn(core_name, rpc_cb_arg, -ENODEV);
		return;
	}

	cache_block_size = spdk_bdev_get_block_size(cache_ctx->base.bdev);
	core_block_size = spdk_bdev_get_block_size(core_ctx->base.bdev);
	if (cache_block_size > core_block_size) {
		SPDK_ERRLOG("OCF core '%s': block size (%d) is less than cache '%s' block size (%d)\n",
			    core_name, core_block_size, cache_name, cache_block_size);
		rc = -ENOTSUP;
		goto err_bsize;
	}

	core_ctx->core_cfg.try_add = vbdev_ocf_core_is_loaded(core_name);

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF core '%s': failed to allocate memory for core add context\n",
			    core_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->cache = cache;
	mngt_ctx->u.core_ctx = core_ctx;

	ocf_mngt_cache_lock(cache, _core_add_rpc_lock_cb, mngt_ctx);

	return;

err_alloc:
err_bsize:
	ocf_mngt_cache_put(cache);
err_ocf_base:
	vbdev_ocf_core_base_detach(core_ctx);
err_no_base:
	vbdev_ocf_core_destroy(core_ctx);
err_create:
err_exist:
err_module:
	rpc_cb_fn(core_name, rpc_cb_arg, rc);
}

static void
_core_remove_rpc_remove_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = mngt_ctx->u.core_ctx;
	ocf_cache_t cache = mngt_ctx->cache;

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

	mngt_ctx->rpc_cb_fn(vbdev_ocf_core_get_name(core_ctx), mngt_ctx->rpc_cb_arg, error);
	ocf_mngt_cache_unlock(cache);
	vbdev_ocf_core_destroy(core_ctx);
	free(mngt_ctx);
}

static void
_core_remove_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	ocf_core_t core = mngt_ctx->core;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating remove of OCF core\n",
		      ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF vbdev '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_core_get_name(core), error);
	}

	/* Do not check core's dirtiness as it was already detached during destruct phase. */
	ocf_mngt_cache_remove_core(core, _core_remove_rpc_remove_cb, mngt_ctx);
}

static void
_core_remove_rpc_unregister_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	ocf_core_t core = mngt_ctx->core;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing unregister of OCF vbdev\n",
		      ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to unregister OCF vbdev during core removal: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-error));
	}

	ocf_mngt_cache_lock(ocf_core_get_cache(core), _core_remove_rpc_lock_cb, mngt_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_core_remove(const char *core_name, vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	ocf_core_t core;
	struct vbdev_ocf_core *core_ctx;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating removal\n", core_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

	/* If core was not added yet due to lack of base or cache device,
	 * just free its structs (and detach its base if exists) and exit. */
	if ((core_ctx = vbdev_ocf_core_waitlist_get_by_name(core_name))) {
		vbdev_ocf_core_waitlist_remove(core_ctx);
		if (vbdev_ocf_core_is_base_attached(core_ctx)) {
			vbdev_ocf_core_base_detach(core_ctx);
		}
		vbdev_ocf_core_destroy(core_ctx);
		rpc_cb_fn(core_name, rpc_cb_arg, 0);
		return;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF core '%s': failed to allocate memory for core remove context\n",
			    core_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->bdev_name = core_name;

	if ((rc = vbdev_ocf_bdev_resolve(mngt_ctx))) {
		SPDK_ERRLOG("OCF core '%s': failed to find core of that name: %s\n",
			    core_name, spdk_strerror(-rc));
		goto err_resolve;
	}
	core = mngt_ctx->core;

	/* Check if given core exists and have context assigned.
	 * If there is no context, it means that this core was added
	 * from metadata during cache load and it's just an empty shell. */
	if (!core || !(core_ctx = ocf_core_get_priv(core))) {
		SPDK_ERRLOG("OCF core '%s': not exist\n", core_name);
		rc = -ENXIO;
		goto err_exist;
	}
	cache = ocf_core_get_cache(core);

	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->cache = cache;
	mngt_ctx->u.core_ctx = core_ctx;

	if (!vbdev_ocf_core_is_base_attached(core_ctx)) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': removing detached (no unregister)\n", core_name);

		ocf_mngt_cache_lock(cache, _core_remove_rpc_lock_cb, mngt_ctx);
		return;
	}

	/* Unregister (and detach) core first before removing it
	 * to send hotremove signal to all opened descriptors. */
	if ((rc = vbdev_ocf_core_unregister(core_ctx, _core_remove_rpc_unregister_cb, mngt_ctx))) {
		SPDK_ERRLOG("OCF core '%s': failed to start unregistering OCF vbdev during core removal: %s\n",
			    core_name, spdk_strerror(-rc));
		goto err_unregister;
	}

	return;

err_unregister:
err_exist:
err_resolve:
	free(mngt_ctx);
err_alloc:
err_module:
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
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	ocf_cache_mode_t cache_mode = mngt_ctx->u.cache_mode;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto err;
	}

	if ((rc = ocf_mngt_cache_set_mode(cache, cache_mode))) {
		SPDK_ERRLOG("OCF cache '%s': failed to change cache mode to '%s' (OCF error: %d)\n",
			    ocf_cache_get_name(cache),
			    vbdev_ocf_cachemode_get_name(cache_mode), rc);
		ocf_mngt_cache_unlock(cache);
		goto err;
	}

	SPDK_NOTICELOG("OCF cache '%s': cache mode set to '%s'\n",
		       ocf_cache_get_name(cache), vbdev_ocf_cachemode_get_name(cache_mode));

	ocf_mngt_cache_save(cache, _cache_save_cb, mngt_ctx);

	return;

err:
	mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, rc);
	ocf_mngt_cache_put(cache);
	free(mngt_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_set_cachemode(const char *cache_name, const char *cache_mode,
			vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': setting new cache mode '%s'\n", cache_name, cache_mode);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache mode change context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->u.cache_mode = vbdev_ocf_cachemode_get_by_name(cache_mode);

	ocf_mngt_cache_lock(cache, _cache_mode_lock_cb, mngt_ctx);

	return;

err_alloc:
	ocf_mngt_cache_put(cache);
err_cache:
err_module:
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
		if ((rc = ocf_mngt_cache_promotion_set_param(cache, ocf_promotion_nhit,
				ocf_nhit_insertion_threshold,
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

	SPDK_NOTICELOG("OCF cache '%s': promotion params set\n", ocf_cache_get_name(cache));

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

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

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
err_module:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_cleaning_policy_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	ocf_cache_t cache = mngt_ctx->cache;

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to set cleaning policy (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		mngt_ctx->rpc_cb_fn(ocf_cache_get_name(cache), mngt_ctx->rpc_cb_arg, error);
		ocf_mngt_cache_unlock(cache);
		ocf_mngt_cache_put(cache);
		free(mngt_ctx);
		return;
	}

	SPDK_NOTICELOG("OCF cache '%s': cleaning params set\n", ocf_cache_get_name(cache));

	ocf_mngt_cache_save(cache, _cache_save_cb, mngt_ctx);
}

static void
_cleaning_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto err_lock;
	}

	if (mngt_ctx->u.cleaning.acp_wake_up_time >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_acp, ocf_acp_wake_up_time,
				mngt_ctx->u.cleaning.acp_wake_up_time))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning acp_wake_up_time param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (mngt_ctx->u.cleaning.acp_flush_max_buffers >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_acp, ocf_acp_flush_max_buffers,
				mngt_ctx->u.cleaning.acp_flush_max_buffers))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning acp_flush_max_buffers param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (mngt_ctx->u.cleaning.alru_wake_up_time >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_wake_up_time,
				mngt_ctx->u.cleaning.alru_wake_up_time))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_wake_up_time param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (mngt_ctx->u.cleaning.alru_flush_max_buffers >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_flush_max_buffers,
				mngt_ctx->u.cleaning.alru_flush_max_buffers))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_flush_max_buffers param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (mngt_ctx->u.cleaning.alru_staleness_time >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_stale_buffer_time,
				mngt_ctx->u.cleaning.alru_staleness_time))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_staleness_time param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (mngt_ctx->u.cleaning.alru_activity_threshold >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_activity_threshold,
				mngt_ctx->u.cleaning.alru_activity_threshold))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_activity_threshold param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (mngt_ctx->u.cleaning.alru_max_dirty_ratio >= 0) {
		if ((rc = ocf_mngt_cache_cleaning_set_param(cache, ocf_cleaning_alru, ocf_alru_max_dirty_ratio,
				mngt_ctx->u.cleaning.alru_max_dirty_ratio))) {
			SPDK_ERRLOG("OCF cache '%s': failed to set cleaning alru_max_dirty_ratio param (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
			goto err_param;
		}
	}

	if (mngt_ctx->u.cleaning.policy >= ocf_cleaning_nop &&
	    mngt_ctx->u.cleaning.policy < ocf_cleaning_max) {
		ocf_mngt_cache_cleaning_set_policy(cache, mngt_ctx->u.cleaning.policy,
						   _cleaning_policy_cb, mngt_ctx);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': cleaning params set\n", ocf_cache_get_name(cache));

		ocf_mngt_cache_save(cache, _cache_save_cb, mngt_ctx);
	}

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
vbdev_ocf_set_cleaning(const char *cache_name, const char *policy, int32_t acp_wake_up_time,
		       int32_t acp_flush_max_buffers, int32_t alru_wake_up_time,
		       int32_t alru_flush_max_buffers, int32_t alru_staleness_time,
		       int32_t alru_activity_threshold, int32_t alru_max_dirty_ratio,
		       vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': setting cleaning params\n", cache_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cleaning set context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->cache = cache;
	mngt_ctx->u.cleaning.policy = vbdev_ocf_cleaning_policy_get_by_name(policy);
	mngt_ctx->u.cleaning.acp_wake_up_time = acp_wake_up_time;
	mngt_ctx->u.cleaning.acp_flush_max_buffers = acp_flush_max_buffers;
	mngt_ctx->u.cleaning.alru_wake_up_time = alru_wake_up_time;
	mngt_ctx->u.cleaning.alru_flush_max_buffers = alru_flush_max_buffers;
	mngt_ctx->u.cleaning.alru_staleness_time = alru_staleness_time;
	mngt_ctx->u.cleaning.alru_activity_threshold = alru_activity_threshold;
	mngt_ctx->u.cleaning.alru_max_dirty_ratio = alru_max_dirty_ratio;

	ocf_mngt_cache_lock(cache, _cleaning_lock_cb, mngt_ctx);

	return;

err_alloc:
	ocf_mngt_cache_put(cache);
err_cache:
err_module:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_seqcutoff_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	ocf_core_t core = mngt_ctx->core;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto err_lock;
	}

	if (core) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': setting sequential cut-off on core device\n",
			      ocf_core_get_name(core));

		if (mngt_ctx->u.seqcutoff.policy >= ocf_seq_cutoff_policy_always &&
		    mngt_ctx->u.seqcutoff.policy < ocf_seq_cutoff_policy_max) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_policy(core, mngt_ctx->u.seqcutoff.policy))) {
				SPDK_ERRLOG("OCF core '%s': failed to set sequential cut-off policy (OCF error: %d)\n",
					    ocf_core_get_name(core), rc);
				goto err_param;
			}
		}

		if (mngt_ctx->u.seqcutoff.threshold >= 0) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_threshold(core, mngt_ctx->u.seqcutoff.threshold * KiB))) {
				SPDK_ERRLOG("OCF core '%s': failed to set sequential cut-off threshold (OCF error: %d)\n",
					    ocf_core_get_name(core), rc);
				goto err_param;
			}
		}

		if (mngt_ctx->u.seqcutoff.promotion_count >= 0) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_promotion_count(core,
					mngt_ctx->u.seqcutoff.promotion_count))) {
				SPDK_ERRLOG("OCF core '%s': failed to set sequential cut-off promotion_count (OCF error: %d)\n",
					    ocf_core_get_name(core), rc);
				goto err_param;
			}
		}

		if (mngt_ctx->u.seqcutoff.promote_on_threshold >= 0) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_promote_on_threshold(core,
					mngt_ctx->u.seqcutoff.promote_on_threshold))) {
				SPDK_ERRLOG("OCF core '%s': failed to set sequential cut-off promote_on_threshold (OCF error: %d)\n",
					    ocf_core_get_name(core), rc);
				goto err_param;
			}
		}

		SPDK_NOTICELOG("OCF core '%s': sequential cut-off params set\n", ocf_core_get_name(core));
	} else {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': setting sequential cut-off on all cores in cache device\n",
			      ocf_cache_get_name(cache));

		if (mngt_ctx->u.seqcutoff.policy >= ocf_seq_cutoff_policy_always &&
		    mngt_ctx->u.seqcutoff.policy < ocf_seq_cutoff_policy_max) {
			if ((rc = ocf_mngt_core_set_seq_cutoff_policy_all(cache, mngt_ctx->u.seqcutoff.policy))) {
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

		SPDK_NOTICELOG("OCF cache '%s': sequential cut-off params set\n", ocf_cache_get_name(cache));
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

/* RPC entry point. */
void
vbdev_ocf_set_seqcutoff(const char *bdev_name, const char *policy, int32_t threshold,
			int32_t promotion_count, int32_t promote_on_threshold,
			vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': setting sequential cut-off params\n", bdev_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rpc_cb_fn(bdev_name, rpc_cb_arg, -EPERM);
		return;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF '%s': failed to allocate memory for sequential cut-off set context\n",
			    bdev_name);
		rpc_cb_fn(bdev_name, rpc_cb_arg, -ENOMEM);
		return;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->bdev_name = bdev_name;
	/* Cache or core will be set using vbdev_ocf_bdev_resolve(). */
	mngt_ctx->cache = NULL;
	mngt_ctx->core = NULL;
	mngt_ctx->u.seqcutoff.policy = vbdev_ocf_seqcutoff_policy_get_by_name(policy);
	mngt_ctx->u.seqcutoff.threshold = threshold;
	mngt_ctx->u.seqcutoff.promotion_count = promotion_count;
	mngt_ctx->u.seqcutoff.promote_on_threshold = promote_on_threshold;

	if ((rc = vbdev_ocf_bdev_resolve(mngt_ctx))) {
		SPDK_ERRLOG("OCF '%s': failed to find cache or core of that name: %s\n",
			    bdev_name, spdk_strerror(-rc));
		rpc_cb_fn(bdev_name, rpc_cb_arg, rc);
		free(mngt_ctx);
		return;
	}

	ocf_mngt_cache_lock(mngt_ctx->cache ? : ocf_core_get_cache(mngt_ctx->core),
			    _seqcutoff_lock_cb, mngt_ctx);
}

static void
_flush_cache_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing flush operation\n",
		      ocf_cache_get_name(cache));

	SPDK_NOTICELOG("OCF cache '%s': flushed\n", ocf_cache_get_name(cache));

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

	SPDK_NOTICELOG("OCF core '%s': flushed\n", ocf_core_get_name(core));

	ocf_mngt_cache_read_unlock(ocf_core_get_cache(core));

	core_ctx->flush.error = error;
	core_ctx->flush.in_progress = false;
}

static void
_flush_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	ocf_core_t core = mngt_ctx->core;
	struct vbdev_ocf_cache *cache_ctx;
	struct vbdev_ocf_core *core_ctx;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto end;
	}

	if (core) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': flushing...\n", ocf_core_get_name(core));

		core_ctx = ocf_core_get_priv(core);
		core_ctx->flush.in_progress = true;
		ocf_mngt_core_flush(core, _flush_core_cb, NULL);
	} else {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': flushing...\n", ocf_cache_get_name(cache));

		cache_ctx = ocf_cache_get_priv(cache);
		cache_ctx->flush.in_progress = true;
		ocf_mngt_cache_flush(cache, _flush_cache_cb, NULL);
	}

end:
	/* Flushing process may take some time to finish, so call RPC callback now and
	 * leave flush running in background. Current status of flushing is dumped in
	 * the bdev_ocf_get_bdevs RPC call output. */
	mngt_ctx->rpc_cb_fn(mngt_ctx->bdev_name, mngt_ctx->rpc_cb_arg, rc);
	free(mngt_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_flush_start(const char *bdev_name, vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': initiating flush operation\n", bdev_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rpc_cb_fn(bdev_name, rpc_cb_arg, -EPERM);
		return;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF '%s': failed to allocate memory for flush context\n", bdev_name);
		rpc_cb_fn(bdev_name, rpc_cb_arg, -ENOMEM);
		return;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->bdev_name = bdev_name;
	/* Cache or core will be set using vbdev_ocf_bdev_resolve(). */
	mngt_ctx->cache = NULL;
	mngt_ctx->core = NULL;

	if ((rc = vbdev_ocf_bdev_resolve(mngt_ctx))) {
		SPDK_ERRLOG("OCF '%s': failed to find cache or core of that name: %s\n",
			    bdev_name, spdk_strerror(-rc));
		rpc_cb_fn(bdev_name, rpc_cb_arg, rc);
		free(mngt_ctx);
		return;
	}

	ocf_mngt_cache_read_lock(mngt_ctx->cache ? : ocf_core_get_cache(mngt_ctx->core),
				 _flush_lock_cb, mngt_ctx);
}

static void
_get_stats_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	struct spdk_json_write_ctx *w = mngt_ctx->u.rpc_dump.rpc_cb_arg;
	ocf_core_t core = mngt_ctx->core;
	struct vbdev_ocf_stats stats;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto end;
	}

	if (core) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': collecting statistics\n", ocf_core_get_name(core));

		if ((rc = vbdev_ocf_stats_core_get(core, &stats))) {
			SPDK_ERRLOG("OCF core '%s': failed to collect statistics (OCF error: %d)\n",
				    ocf_core_get_name(core), rc);
		}
	} else {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': collecting statistics\n", ocf_cache_get_name(cache));

		if ((rc = vbdev_ocf_stats_cache_get(cache, &stats))) {
			SPDK_ERRLOG("OCF cache '%s': failed to collect statistics (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
		}
	}

	if (!rc) {
		vbdev_ocf_stats_write_json(w, &stats);
	}

	ocf_mngt_cache_read_unlock(cache);

end:
	mngt_ctx->u.rpc_dump.rpc_cb_fn(mngt_ctx->u.rpc_dump.rpc_cb_arg, mngt_ctx->rpc_cb_arg);
	free(mngt_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_get_stats(const char *bdev_name, vbdev_ocf_rpc_dump_cb rpc_cb_fn,
		    void *rpc_cb_arg1, void *rpc_cb_arg2)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': getting statistics\n", bdev_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		goto err_module;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF '%s': failed to allocate memory for getting statistics context\n",
			    bdev_name);
		goto err_alloc;
	}
	mngt_ctx->u.rpc_dump.rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->u.rpc_dump.rpc_cb_arg = rpc_cb_arg1;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg2;
	mngt_ctx->bdev_name = bdev_name;
	/* Cache or core will be set using vbdev_ocf_bdev_resolve(). */
	mngt_ctx->cache = NULL;
	mngt_ctx->core = NULL;

	if ((rc = vbdev_ocf_bdev_resolve(mngt_ctx))) {
		SPDK_ERRLOG("OCF '%s': failed to find cache or core of that name: %s\n",
			    bdev_name, spdk_strerror(-rc));
		goto err_resolve;
	}

	ocf_mngt_cache_read_lock(mngt_ctx->cache ? : ocf_core_get_cache(mngt_ctx->core),
				 _get_stats_lock_cb, mngt_ctx);

	return;

err_resolve:
	free(mngt_ctx);
err_alloc:
err_module:
	rpc_cb_fn(rpc_cb_arg1, rpc_cb_arg2);
}

static void
_reset_stats_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx = cb_arg;
	ocf_core_t core = mngt_ctx->core;
	int rc;

	if ((rc = error)) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		goto end;
	}

	if (core) {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': resetting statistics\n", ocf_core_get_name(core));

		if ((rc = vbdev_ocf_stats_core_reset(core))) {
			SPDK_ERRLOG("OCF core '%s': failed to reset statistics (OCF error: %d)\n",
				    ocf_core_get_name(core), rc);
		}
	} else {
		SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': resetting statistics\n", ocf_cache_get_name(cache));

		if ((rc = vbdev_ocf_stats_cache_reset(cache))) {
			SPDK_ERRLOG("OCF cache '%s': failed to reset statistics (OCF error: %d)\n",
				    ocf_cache_get_name(cache), rc);
		}
	}

	ocf_mngt_cache_unlock(cache);

end:
	mngt_ctx->rpc_cb_fn(mngt_ctx->bdev_name, mngt_ctx->rpc_cb_arg, rc);
	free(mngt_ctx);
}

/* RPC entry point. */
void
vbdev_ocf_reset_stats(const char *bdev_name, vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF '%s': resetting statistics\n", bdev_name);

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		rc = -EPERM;
		goto err_module;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF '%s': failed to allocate memory for resetting statistics context\n",
			    bdev_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	mngt_ctx->rpc_cb_fn = rpc_cb_fn;
	mngt_ctx->rpc_cb_arg = rpc_cb_arg;
	mngt_ctx->bdev_name = bdev_name;
	/* Cache or core will be set using vbdev_ocf_bdev_resolve(). */
	mngt_ctx->cache = NULL;
	mngt_ctx->core = NULL;

	if ((rc = vbdev_ocf_bdev_resolve(mngt_ctx))) {
		SPDK_ERRLOG("OCF '%s': failed to find cache or core of that name: %s\n",
			    bdev_name, spdk_strerror(-rc));
		goto err_resolve;
	}

	ocf_mngt_cache_lock(mngt_ctx->cache ? : ocf_core_get_cache(mngt_ctx->core),
			    _reset_stats_lock_cb, mngt_ctx);

	return;

err_resolve:
	free(mngt_ctx);
err_alloc:
err_module:
	rpc_cb_fn(bdev_name, rpc_cb_arg, rc);
}

static int
dump_promotion_info(struct spdk_json_write_ctx *w, ocf_cache_t cache)
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
dump_cleaning_info(struct spdk_json_write_ctx *w, ocf_cache_t cache)
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
dump_seqcutoff_info(struct spdk_json_write_ctx *w, ocf_core_t core)
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
	spdk_json_write_named_uint32(w, "threshold", param_val_int);

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

static int
dump_core_waitlist_info(struct spdk_json_write_ctx *w, struct vbdev_ocf_core *core_ctx)
{
	spdk_json_write_named_string(w, "name", vbdev_ocf_core_get_name(core_ctx));
	spdk_json_write_named_string(w, "cache_name", core_ctx->cache_name);
	spdk_json_write_named_string(w, "base_name", core_ctx->base.name);
	spdk_json_write_named_bool(w, "base_attached", vbdev_ocf_core_is_base_attached(core_ctx));
	if (vbdev_ocf_core_is_base_attached(core_ctx)) {
		spdk_json_write_named_uint64(w, "size", spdk_bdev_get_block_size(core_ctx->base.bdev) *
					     spdk_bdev_get_num_blocks(core_ctx->base.bdev));
		spdk_json_write_named_uint32(w, "block_size", spdk_bdev_get_block_size(core_ctx->base.bdev));
	} else {
		spdk_json_write_named_null(w, "size");
		spdk_json_write_named_null(w, "block_size");
	}

	return 0;
}

static int
dump_core_info(struct spdk_json_write_ctx *w, ocf_core_t core)
{
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	int rc;

	spdk_json_write_named_string(w, "name", ocf_core_get_name(core));
	spdk_json_write_named_string(w, "cache_name", ocf_cache_get_name(ocf_core_get_cache(core)));
	spdk_json_write_named_string(w, "base_name", core_ctx ? core_ctx->base.name : "");
	spdk_json_write_named_bool(w, "base_attached",
				   core_ctx ? vbdev_ocf_core_is_base_attached(core_ctx) : false);
	if (core_ctx && vbdev_ocf_core_is_base_attached(core_ctx)) {
		spdk_json_write_named_uint64(w, "size", spdk_bdev_get_block_size(core_ctx->base.bdev) *
					     spdk_bdev_get_num_blocks(core_ctx->base.bdev));
		spdk_json_write_named_uint32(w, "block_size", spdk_bdev_get_block_size(core_ctx->base.bdev));
	} else {
		spdk_json_write_named_null(w, "size");
		spdk_json_write_named_null(w, "block_size");
	}
	spdk_json_write_named_bool(w, "loading", !core_ctx);

	spdk_json_write_named_object_begin(w, "seq_cutoff");
	if ((rc = dump_seqcutoff_info(w, core))) {
		SPDK_ERRLOG("OCF core '%s': failed to get sequential cut-off params info: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-rc));
	}
	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "flush");
	spdk_json_write_named_bool(w, "in_progress", core_ctx ? core_ctx->flush.in_progress : false);
	spdk_json_write_named_int32(w, "error", core_ctx ? core_ctx->flush.error : 0);
	spdk_json_write_object_end(w);

	return rc;
}

static int
dump_cache_info(struct spdk_json_write_ctx *w, ocf_cache_t cache)
{
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);
	int rc;

	spdk_json_write_named_string(w, "name", ocf_cache_get_name(cache));
	spdk_json_write_named_string(w, "base_name", cache_ctx->base.name);
	spdk_json_write_named_bool(w, "base_attached", ocf_cache_is_device_attached(cache) &&
				   vbdev_ocf_cache_is_base_attached(cache));
	if (vbdev_ocf_cache_is_base_attached(cache)) {
		spdk_json_write_named_uint64(w, "size", spdk_bdev_get_block_size(cache_ctx->base.bdev) *
					     spdk_bdev_get_num_blocks(cache_ctx->base.bdev));
		spdk_json_write_named_uint32(w, "block_size", spdk_bdev_get_block_size(cache_ctx->base.bdev));
	} else {
		spdk_json_write_named_null(w, "size");
		spdk_json_write_named_null(w, "block_size");
	}
	spdk_json_write_named_uint32(w, "cache_line_size", ocf_cache_get_line_size(cache));
	spdk_json_write_named_string(w, "cache_mode",
				     vbdev_ocf_cachemode_get_name(ocf_cache_get_mode(cache)));

	spdk_json_write_named_object_begin(w, "promotion");
	if ((rc = dump_promotion_info(w, cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to get promotion params info: %s\n",
			    ocf_cache_get_name(cache), spdk_strerror(-rc));
	}
	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "cleaning");
	if ((rc = dump_cleaning_info(w, cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to get cleaning params info: %s\n",
			    ocf_cache_get_name(cache), spdk_strerror(-rc));
	}
	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "flush");
	spdk_json_write_named_bool(w, "in_progress", cache_ctx->flush.in_progress);
	spdk_json_write_named_int32(w, "error", cache_ctx->flush.error);
	spdk_json_write_object_end(w);

	spdk_json_write_named_uint16(w, "cores_count", ocf_cache_get_core_count(cache));

	return rc;
}

static int
_get_bdevs_core_visitor(ocf_core_t core, void *cb_arg)
{
	struct spdk_json_write_ctx *w = cb_arg;
	int rc;

	spdk_json_write_object_begin(w);
	if ((rc = dump_core_info(w, core))) {
		SPDK_ERRLOG("OCF core '%s': failed to get core info: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-rc));
	}
	spdk_json_write_object_end(w);

	return rc;
}

static int
_get_bdevs_cache_visitor(ocf_cache_t cache, void *cb_arg)
{
	struct spdk_json_write_ctx *w = cb_arg;
	int rc;

	spdk_json_write_object_begin(w);

	if ((rc = dump_cache_info(w, cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to get cache info: %s\n",
			    ocf_cache_get_name(cache), spdk_strerror(-rc));
	}

	spdk_json_write_named_array_begin(w, "cores");
	rc = ocf_core_visit(cache, _get_bdevs_core_visitor, w, false);
	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);

	return rc;
}

/* RPC entry point. */
void
vbdev_ocf_get_bdevs(const char *bdev_name, vbdev_ocf_rpc_dump_cb rpc_cb_fn, void *rpc_cb_arg1,
		    void *rpc_cb_arg2)
{
	struct spdk_json_write_ctx *w = rpc_cb_arg1;
	struct vbdev_ocf_core *core_ctx;
	struct vbdev_ocf_mngt_ctx *mngt_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF: getting info about vbdevs\n");

	if (!g_vbdev_ocf_module_is_running) {
		SPDK_ERRLOG("OCF: failed to handle the call - module stopping\n");
		goto end;
	}

	if (!bdev_name) {
		spdk_json_write_named_array_begin(w, "cores_waitlist");
		vbdev_ocf_foreach_core_in_waitlist(core_ctx) {
			spdk_json_write_object_begin(w);
			if ((rc = dump_core_waitlist_info(w, core_ctx))) {
				SPDK_ERRLOG("OCF core '%s': failed to get wait list core info: %s\n",
					    vbdev_ocf_core_get_name(core_ctx), spdk_strerror(-rc));
			}
			spdk_json_write_object_end(w);
		}
		spdk_json_write_array_end(w);

		spdk_json_write_named_array_begin(w, "caches");
		if ((rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _get_bdevs_cache_visitor, w))) {
			SPDK_ERRLOG("OCF: failed to iterate over bdevs: %s\n", spdk_strerror(-rc));
		}
		spdk_json_write_array_end(w);

		goto end;
	}

	if ((core_ctx = vbdev_ocf_core_waitlist_get_by_name(bdev_name))) {
		if ((rc = dump_core_waitlist_info(w, core_ctx))) {
			SPDK_ERRLOG("OCF core '%s': failed to get wait list core info: %s\n",
				    vbdev_ocf_core_get_name(core_ctx), spdk_strerror(-rc));
		}

		goto end;
	}

	mngt_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!mngt_ctx) {
		SPDK_ERRLOG("OCF '%s': failed to allocate memory for getting bdevs info context\n",
			    bdev_name);
		goto end;
	}
	mngt_ctx->bdev_name = bdev_name;
	/* Cache or core will be set using vbdev_ocf_bdev_resolve(). */
	mngt_ctx->cache = NULL;
	mngt_ctx->core = NULL;

	if ((rc = vbdev_ocf_bdev_resolve(mngt_ctx))) {
		SPDK_ERRLOG("OCF '%s': failed to find cache or core of that name: %s\n",
			    bdev_name, spdk_strerror(-rc));
		free(mngt_ctx);
		goto end;
	}

	if (mngt_ctx->core) {
		if ((rc = dump_core_info(w, mngt_ctx->core))) {
			SPDK_ERRLOG("OCF core '%s': failed to get core info: %s\n",
				    ocf_core_get_name(mngt_ctx->core), spdk_strerror(-rc));
		}
	} else {
		if ((rc = dump_cache_info(w, mngt_ctx->cache))) {
			SPDK_ERRLOG("OCF cache '%s': failed to get cache info: %s\n",
				    ocf_cache_get_name(mngt_ctx->cache), spdk_strerror(-rc));
		}
	}

	free(mngt_ctx);

end:
	rpc_cb_fn(rpc_cb_arg1, rpc_cb_arg2);
}

SPDK_LOG_REGISTER_COMPONENT(vbdev_ocf)
