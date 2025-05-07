/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#include <ocf/ocf.h>

#include "spdk/bdev_module.h"
#include "spdk/string.h" // rm ?

#include "vbdev_ocf.h"
#include "ctx.h"
#include "data.h"
#include "volume.h"

/* This namespace UUID was generated using uuid_generate() method. */
#define BDEV_OCF_NAMESPACE_UUID "f92b7f49-f6c0-44c8-bd23-3205e8c3b6ad"

static int vbdev_ocf_module_init(void);
static void vbdev_ocf_module_fini_start(void);
static void vbdev_ocf_module_fini(void);
static int vbdev_ocf_module_get_ctx_size(void);
static void vbdev_ocf_module_examine_config(struct spdk_bdev *bdev);

struct spdk_bdev_module ocf_if = {
	.name = "OCF",
	.module_init = vbdev_ocf_module_init,
	.fini_start = vbdev_ocf_module_fini_start,
	.module_fini = vbdev_ocf_module_fini,
	.get_ctx_size = vbdev_ocf_module_get_ctx_size,
	.examine_config = vbdev_ocf_module_examine_config,
	.examine_disk = NULL, // todo ?
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
_module_fini_cache_stop_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing stop of OCF cache\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to stop OCF cache (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	}

	vbdev_ocf_cache_base_detach(cache);
	vbdev_ocf_cache_destroy(cache);
	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);

	if (!ocf_mngt_cache_get_count(vbdev_ocf_ctx)) {
		spdk_bdev_module_fini_start_done();
	}
}

static void
_module_fini_cache_flush_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to flush OCF cache (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	}

	ocf_mngt_cache_stop(cache, _module_fini_cache_stop_cb, cb_arg);
}

static void
_module_fini_cache_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	}

	if (ocf_mngt_cache_is_dirty(cache)) {
		ocf_mngt_cache_flush(cache, _module_fini_cache_flush_cb, cb_arg);
	} else {
		ocf_mngt_cache_stop(cache, _module_fini_cache_stop_cb, cb_arg);
	}
}

static int
_module_fini_cache_visit_stop(ocf_cache_t cache, void *cb_arg)
{
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating stop of OCF cache\n",
		      ocf_cache_get_name(cache));

	printf("*** (cache stop) cache refcnt before get: %d\n", ocf_cache_get_refcnt(cache));
	if ((rc = ocf_mngt_cache_get(cache))) {
		SPDK_ERRLOG("OCF cache '%s': failed to increment ref count: %s\n",
			    ocf_cache_get_name(cache), spdk_strerror(-rc));
		return rc;
	}
	printf("*** (cache stop) cache refcnt after get: %d\n", ocf_cache_get_refcnt(cache));

	ocf_mngt_cache_lock(cache, _module_fini_cache_lock_cb, cb_arg);

	return rc;
}

static void
_module_fini_core_unregister_cb(void *cb_arg, int error)
{
	ocf_core_t core = cb_arg;
	ocf_cache_t cache = ocf_core_get_cache(core);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing unregister of OCF vbdev\n",
		      ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to unregister OCF vbdev during module stop: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-error));
	}

	printf("*** (core unregister) cache refcnt before put: %d\n", ocf_cache_get_refcnt(cache));
	ocf_mngt_cache_put(cache); // needed ?
	printf("*** (core unregister) cache refcnt after put: %d\n", ocf_cache_get_refcnt(cache));

	//if (ocf_cache_get_refcnt(cache) < 2) {
	//	ocf_mngt_cache_lock(cache, _module_fini_cache_lock_cb, cb_arg);
	//}
}

static int
_module_fini_core_visit(ocf_core_t core, void *cb_arg)
{
	ocf_cache_t cache = ocf_core_get_cache(core);
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	int rc = 0;

	printf("*** (core unregister) cache refcnt before get: %d\n", ocf_cache_get_refcnt(cache));
	if ((rc = ocf_mngt_cache_get(cache))) {
		SPDK_ERRLOG("OCF core '%s': failed to increment cache '%s' ref count: %s\n",
			    ocf_core_get_name(core), ocf_cache_get_name(cache), spdk_strerror(-rc));
		return rc;
	}
	printf("*** (core unregister) cache refcnt after get: %d\n", ocf_cache_get_refcnt(cache));

	if ((rc = vbdev_ocf_core_unregister(core_ctx, _module_fini_core_unregister_cb, core))) {
		SPDK_ERRLOG("OCF core '%s': failed to start unregistering OCF vbdev during module stop: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-rc));
		return rc;
	}

	return rc;
}

static int
_module_fini_cache_visit(ocf_cache_t cache, void *cb_arg)
{
	int rc = 0;

	if ((rc = ocf_core_visit(cache, _module_fini_core_visit, cb_arg, false))) { // only opened cores ?
		SPDK_ERRLOG("OCF: failed to iterate over core bdevs: %s\n", spdk_strerror(-rc));
		return rc;
	}

	return rc;
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

	// async fuckup ? (move to unregister_cb and do by refcnt)
	if ((rc = ocf_mngt_cache_visit(vbdev_ocf_ctx, _module_fini_cache_visit_stop, NULL))) {
		SPDK_ERRLOG("OCF: failed to iterate over cache bdevs: %s\n", spdk_strerror(-rc));
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

static void
vbdev_ocf_module_examine_config(struct spdk_bdev *bdev)
{
	//const char *bdev_name = spdk_bdev_get_name(bdev);
	//struct vbdev_ocf_cache *cache;
	//struct vbdev_ocf_core *core;
	//int rc;

	//vbdev_ocf_foreach_cache(cache) {
	//	if (!vbdev_ocf_cache_is_incomplete(cache)) {
	//		continue;
	//	}
	//	if (strcmp(bdev_name, cache->init_params->bdev_name)) {
	//		continue;
	//	}

	//	assert(!vbdev_ocf_cache_is_base_attached(cache));

	//	if ((rc = vbdev_ocf_cache_base_attach(cache, bdev_name))) {
	//		SPDK_ERRLOG("OCF cache '%s': failed to attach base bdev '%s'\n",
	//			    cache->name, bdev_name);
	//	}

	//	printf("*** START OCF cache\n");

	//	vbdev_ocf_cache_remove_incomplete(cache);

	//	goto end;
	//}

	//vbdev_ocf_foreach_core_in_waitlist(core) {
	//	if (strcmp(bdev_name, core->init_params->bdev_name)) {
	//		continue;
	//	}

	//	assert(!vbdev_ocf_core_is_base_attached(core));

	//	if ((rc = vbdev_ocf_core_base_attach(core, bdev_name))) {
	//		SPDK_ERRLOG("OCF core '%s': failed to attach base bdev '%s'\n",
	//			    core->name, bdev_name);
	//	}

	//	cache = vbdev_ocf_cache_get_by_name(core->init_params->cache_name);
	//	if (!cache || vbdev_ocf_cache_is_incomplete(cache)) {
	//		SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for OCF cache '%s'\n",
	//			       core->name, core->init_params->cache_name);
	//		goto end;
	//	}

	//	printf("*** ADD OCF core\n");

	//	vbdev_ocf_core_add_to_cache(core, cache);
	//	vbdev_ocf_core_remove_incomplete(core);

	//	goto end;
	//}

//end:
	spdk_bdev_module_examine_done(&ocf_if);
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

	// rm ?
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

	// rm ?
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
	//ocf_mngt_cache_put(cache); // no need ? (it's already stopped)
}

static void
_cache_start_rpc_attach_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_start_ctx = cb_arg;
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing start\n", ocf_cache_get_name(cache));

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
		vbdev_ocf_queue_put(cache_ctx->cache_mngt_q);
		ocf_mngt_cache_stop(cache, _cache_start_rpc_err_cb, NULL);
		if (cache_start_ctx->rpc_cb_fn) {
			cache_start_ctx->rpc_cb_fn(NULL, cache_start_ctx->rpc_cb_arg, error); // cache name
		}
		free(cache_start_ctx);

		return;
	}

	SPDK_NOTICELOG("OCF cache '%s': started\n", ocf_cache_get_name(cache));

	// check for cores in g_vbdev_ocf_core_waitlist
	// check if (cache_block_size > core_block_size)
	
	ocf_mngt_cache_unlock(cache);
	if (cache_start_ctx->rpc_cb_fn) {
		cache_start_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cache_start_ctx->rpc_cb_arg, 0);
	}
	free(cache_start_ctx);
}

static void
_cache_start_rpc_metadata_probe_cb(void *priv, int error, struct ocf_metadata_probe_status *status)
{
	struct vbdev_ocf_mngt_ctx *cache_start_ctx = priv;
	ocf_cache_t cache = cache_start_ctx->cache;
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	ocf_volume_close(cache_ctx->cache_att_cfg.device.volume);

	if (error && error != -OCF_ERR_NO_METADATA) {
		SPDK_ERRLOG("OCF cache '%s': failed to probe metadata\n", ocf_cache_get_name(cache));
		_cache_start_rpc_attach_cb(cache, cache_start_ctx, error);
	}

	if (error == -OCF_ERR_NO_METADATA) {
		SPDK_NOTICELOG("OCF cache '%s': metadata not found - starting new cache\n",
			       ocf_cache_get_name(cache));
		//cache_ctx->cache_att_cfg.force = true; // needed ?
		ocf_mngt_cache_attach(cache, &cache_ctx->cache_att_cfg,
				      _cache_start_rpc_attach_cb, cache_start_ctx);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': metadata found - loading cache from metadata\n",
			       ocf_cache_get_name(cache));
		// check status for cache_name/mode/line_size/dirty ?
		ocf_mngt_cache_load(cache, &cache_ctx->cache_att_cfg,
				    _cache_start_rpc_attach_cb, cache_start_ctx);
	}
}

/* RPC entry point. */
void
vbdev_ocf_cache_start(const char *cache_name, const char *base_name,
		      const char *cache_mode, const uint8_t cache_line_size, bool no_load,
		      vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_cache *cache_ctx;
	struct vbdev_ocf_mngt_ctx *cache_start_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating start\n", cache_name);

	if (vbdev_ocf_bdev_exists(cache_name)) {
		SPDK_ERRLOG("OCF: bdev '%s' already exists\n", cache_name);
		rc = -EEXIST;
		goto err_create;
	}

	if ((rc = vbdev_ocf_cache_create(&cache, cache_name, cache_mode, cache_line_size, no_load))) {
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
		SPDK_ERRLOG("OCF cache '%s': failed to open base bdev '%s'\n", cache_name, base_name);
		goto err_base;
	}

	cache_start_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!cache_start_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache start context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	cache_start_ctx->cache = cache;
	cache_start_ctx->rpc_cb_fn = rpc_cb_fn;
	cache_start_ctx->rpc_cb_arg = rpc_cb_arg;

	cache_ctx = ocf_cache_get_priv(cache);

	if (no_load) {
		ocf_mngt_cache_attach(cache, &cache_ctx->cache_att_cfg, _cache_start_rpc_attach_cb,
				      cache_start_ctx);
		return;
	}

	if ((rc = ocf_volume_open(cache_ctx->cache_att_cfg.device.volume, &cache_ctx->base))) {
		SPDK_ERRLOG("OCF cache '%s': failed to open volume\n", ocf_cache_get_name(cache));
		goto err_open;
	}

	ocf_metadata_probe(vbdev_ocf_ctx, cache_ctx->cache_att_cfg.device.volume,
			   _cache_start_rpc_metadata_probe_cb, cache_start_ctx);

	return;

err_open:
	free(cache_start_ctx);
err_alloc:
	vbdev_ocf_cache_base_detach(cache);
err_base:
	vbdev_ocf_queue_put(((struct vbdev_ocf_cache *)ocf_cache_get_priv(cache))->cache_mngt_q);
err_queue:
	ocf_mngt_cache_stop(cache, _cache_start_rpc_err_cb, NULL);
err_create:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_cache_stop_rpc_stop_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_stop_ctx = cb_arg;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing stop of OCF cache\n",
		      ocf_cache_get_name(cache));
	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': finishing stop\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to stop OCF cache (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': stopped\n", ocf_cache_get_name(cache));
		vbdev_ocf_cache_base_detach(cache);
		vbdev_ocf_cache_destroy(cache);
	}

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	cache_stop_ctx->rpc_cb_fn(NULL, cache_stop_ctx->rpc_cb_arg, error); // cache name
	free(cache_stop_ctx);
}

static void
_cache_stop_rpc_flush_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to flush OCF cache (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	}

	ocf_mngt_cache_stop(cache, _cache_stop_rpc_stop_cb, cb_arg);
}

static void
_cache_stop_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating stop of OCF cache\n",
		      ocf_cache_get_name(cache));

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
	}

	// no need to manually flush ?
	if (ocf_mngt_cache_is_dirty(cache)) {
		ocf_mngt_cache_flush(cache, _cache_stop_rpc_flush_cb, cb_arg);
	} else {
		ocf_mngt_cache_stop(cache, _cache_stop_rpc_stop_cb, cb_arg);
	}
}

static void
_cache_stop_rpc_core_unregister_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_stop_ctx = cb_arg;
	ocf_cache_t cache = cache_stop_ctx->cache;
	ocf_core_t core = cache_stop_ctx->core;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': finishing unregister of OCF vbdev\n",
		      ocf_core_get_name(core));

	if (error) {
		SPDK_ERRLOG("OCF core '%s': failed to unregister OCF vbdev during cache stop: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-error));
	}

	printf("*** (core unregister) cache refcnt before put: %d\n", ocf_cache_get_refcnt(cache));
	ocf_mngt_cache_put(cache);
	printf("*** (core unregister) cache refcnt after put: %d\n", ocf_cache_get_refcnt(cache));

	if (ocf_cache_get_refcnt(cache) < 2) {
		ocf_mngt_cache_lock(cache, _cache_stop_rpc_lock_cb, cb_arg);
	}
}

static int
_cache_stop_rpc_core_visit(ocf_core_t core, void *cb_arg)
{
	struct vbdev_ocf_mngt_ctx *cache_stop_ctx = cb_arg;
	ocf_cache_t cache = ocf_core_get_cache(core);
	struct vbdev_ocf_core *core_ctx = ocf_core_get_priv(core);
	int rc = 0;

	// async fuckup ?
	cache_stop_ctx->core = core;

	printf("*** (core unregister) cache refcnt before get: %d\n", ocf_cache_get_refcnt(cache));
	if ((rc = ocf_mngt_cache_get(cache))) {
		SPDK_ERRLOG("OCF core '%s': failed to increment cache '%s' ref count: %s\n",
			    ocf_core_get_name(core), ocf_cache_get_name(cache), spdk_strerror(-rc));
		return rc;
	}
	printf("*** (core unregister) cache refcnt after get: %d\n", ocf_cache_get_refcnt(cache));

	if ((rc = vbdev_ocf_core_unregister(core_ctx, _cache_stop_rpc_core_unregister_cb, cache_stop_ctx))) {
		SPDK_ERRLOG("OCF core '%s': failed to start unregistering OCF vbdev during cache stop: %s\n",
			    ocf_core_get_name(core), spdk_strerror(-rc));
		return rc;
	}

	return rc;
}

/* RPC entry point. */
void
vbdev_ocf_cache_stop(const char *cache_name, vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg)
{
	ocf_cache_t cache;
	struct vbdev_ocf_mngt_ctx *cache_stop_ctx;
	int rc;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF cache '%s': initiating stop\n", cache_name);

	// increments cache refcnt !!!
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

	if (ocf_cache_get_core_count(cache) > 0) {
		if ((rc = ocf_core_visit(cache, _cache_stop_rpc_core_visit, cache_stop_ctx, true))) {
			SPDK_ERRLOG("OCF: failed to iterate over core bdevs: %s\n", spdk_strerror(-rc));
			// ocf_mngt_cache_lock(...) ?
		}
	} else {
		ocf_mngt_cache_lock(cache, _cache_stop_rpc_lock_cb, cache_stop_ctx);
	}

	return;

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
		SPDK_NOTICELOG("OCF cache '%s': detached\n", ocf_cache_get_name(cache));
	}

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

	// increments cache refcnt !!!
	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		SPDK_ERRLOG("OCF cache '%s': not exist\n", cache_name);
		rc = -ENXIO;
		goto err_cache;
	}

	if (ocf_cache_is_detached(cache)) {
		SPDK_ERRLOG("OCF cache '%s': device already detached\n", cache_name);
		rc = -EALREADY; // better errno ?
		goto err_state;
	}

	cache_detach_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!cache_detach_ctx) {
		SPDK_ERRLOG("OCF cache '%s': failed to allocate memory for cache detach context\n",
			    cache_name);
		rc = -ENOMEM;
		goto err_state;
	}
	cache_detach_ctx->rpc_cb_fn = rpc_cb_fn;
	cache_detach_ctx->rpc_cb_arg = rpc_cb_arg;

	ocf_mngt_cache_lock(cache, _cache_detach_rpc_lock_cb, cache_detach_ctx);

	return;

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

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to attach OCF cache device (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
		vbdev_ocf_cache_base_detach(cache);
	} else {
		SPDK_NOTICELOG("OCF cache '%s': attached\n", ocf_cache_get_name(cache));
	}

	ocf_mngt_cache_unlock(cache);
	ocf_mngt_cache_put(cache);
	cache_attach_ctx->rpc_cb_fn(ocf_cache_get_name(cache), cache_attach_ctx->rpc_cb_arg, error);
	free(cache_attach_ctx);
}

static void
_cache_attach_rpc_lock_cb(ocf_cache_t cache, void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *cache_attach_ctx = cb_arg;
	struct vbdev_ocf_cache *cache_ctx = ocf_cache_get_priv(cache);

	if (error) {
		SPDK_ERRLOG("OCF cache '%s': failed to acquire OCF cache lock (OCF error: %d)\n",
			    ocf_cache_get_name(cache), error);
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

	// increments cache refcnt !!! (put it right away ?)
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

	if ((rc = vbdev_ocf_cache_base_attach(cache, base_name))) {
		if (rc == -ENODEV) {
			SPDK_NOTICELOG("OCF cache '%s': start deferred - waiting for base bdev '%s'\n",
				       cache_name, base_name);
			rpc_cb_fn(cache_name, rpc_cb_arg, -ENODEV);
			return;
		}
		SPDK_ERRLOG("OCF cache '%s': failed to open base bdev '%s'\n", cache_name, base_name);
		goto err_state;
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

	cache_ctx = ocf_cache_get_priv(cache);
	cache_ctx->cache_att_cfg.force = force;

	ocf_mngt_cache_lock(cache, _cache_attach_rpc_lock_cb, cache_attach_ctx);

	return;

err_alloc:
	vbdev_ocf_cache_base_detach(cache);
err_state:
	ocf_mngt_cache_put(cache);
err_cache:
	rpc_cb_fn(cache_name, rpc_cb_arg, rc);
}

static void
_core_add_rpc_err_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *core_add_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = core_add_ctx->core_ctx;
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
	struct vbdev_ocf_core *core_ctx = core_add_ctx->core_ctx;
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
	struct vbdev_ocf_core *core_ctx = core_add_ctx->core_ctx;

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
	struct vbdev_ocf_core *core_ctx;
	struct vbdev_ocf_mngt_ctx *core_add_ctx;
	int rc = 0;

	SPDK_DEBUGLOG(vbdev_ocf, "OCF core '%s': initiating add\n", core_name);

	if (vbdev_ocf_bdev_exists(core_name)) {
		SPDK_ERRLOG("OCF: bdev '%s' already exists\n", core_name);
		rc = -EEXIST;
		goto err_create;
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
		SPDK_ERRLOG("OCF core '%s': failed to open base bdev '%s'\n", core_name, base_name);
		goto err_base;
	}

	/* Second, check if OCF cache for this core is already present and started. */
	if (ocf_mngt_cache_get_by_name(vbdev_ocf_ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		/* If not, just put core context on the temporary core wait list and exit. */
		SPDK_NOTICELOG("OCF core '%s': add deferred - waiting for OCF cache '%s'\n",
			       core_name, cache_name);
		STAILQ_INSERT_TAIL(&g_vbdev_ocf_core_waitlist, core_ctx, waitlist_entry);
		rpc_cb_fn(core_name, rpc_cb_arg, -ENODEV);
		return;
	}

	// check if (cache_block_size > core_block_size)

	core_add_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!core_add_ctx) {
		SPDK_ERRLOG("OCF core '%s': failed to allocate memory for core add context\n",
			    core_name);
		rc = -ENOMEM;
		goto err_alloc;
	}
	core_add_ctx->cache = cache;
	core_add_ctx->core_ctx = core_ctx;
	core_add_ctx->rpc_cb_fn = rpc_cb_fn;
	core_add_ctx->rpc_cb_arg = rpc_cb_arg;

	ocf_mngt_cache_lock(cache, _core_add_rpc_lock_cb, core_add_ctx);

	return;

err_alloc:
	ocf_mngt_cache_put(cache);
	vbdev_ocf_core_base_detach(core_ctx);
err_base:
	vbdev_ocf_core_destroy(core_ctx);
err_create:
	rpc_cb_fn(core_name, rpc_cb_arg, rc);
}

static void
_core_remove_rpc_remove_cb(void *cb_arg, int error)
{
	struct vbdev_ocf_mngt_ctx *core_rm_ctx = cb_arg;
	struct vbdev_ocf_core *core_ctx = core_rm_ctx->core_ctx;
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

	core_rm_ctx = calloc(1, sizeof(struct vbdev_ocf_mngt_ctx));
	if (!core_rm_ctx) {
		SPDK_ERRLOG("OCF core '%s': failed to allocate memory for core remove context\n", core_name);
		rc = -ENOMEM;
		goto err_core;
	}
	core_rm_ctx->cache = cache;
	core_rm_ctx->core = core;
	core_rm_ctx->core_ctx = ocf_core_get_priv(core);
	core_rm_ctx->rpc_cb_fn = rpc_cb_fn;
	core_rm_ctx->rpc_cb_arg = rpc_cb_arg;

	if ((rc = vbdev_ocf_core_unregister(core_rm_ctx->core_ctx, _core_remove_rpc_unregister_cb, core_rm_ctx))) {
		SPDK_ERRLOG("OCF core '%s': failed to start unregistering OCF vbdev during core removal\n",
			    ocf_core_get_name(core));
		goto err_unregister;
	}

	return;

err_unregister:
	free(core_rm_ctx);
err_core:
	ocf_mngt_cache_put(cache);
err_cache:
	rpc_cb_fn(core_name, rpc_cb_arg, rc);
}

//comp
//static void
//_write_cache_info_begin(struct spdk_json_write_ctx *w, struct vbdev_ocf_cache *cache)
//{
//	spdk_json_write_object_begin(w);
//	spdk_json_write_named_string(w, "type", "OCF_cache");
//	spdk_json_write_named_string(w, "name", cache->name);
//	spdk_json_write_named_string(w, "base_bdev_name",
//				     cache->base.bdev ? spdk_bdev_get_name(cache->base.bdev) : "" );
//	spdk_json_write_named_uint16(w, "cores_count", cache->cores_count);
//}
//
//static void
//_write_cache_info_end(struct spdk_json_write_ctx *w, struct vbdev_ocf_cache *cache)
//{
//	spdk_json_write_object_end(w);
//}
//
//static void
//_write_core_info(struct spdk_json_write_ctx *w, struct vbdev_ocf_core *core)
//{
//	spdk_json_write_object_begin(w);
//	spdk_json_write_named_string(w, "type", "OCF_core");
//	spdk_json_write_named_string(w, "name", core->name);
//	spdk_json_write_named_string(w, "base_bdev_name",
//				     core->base.bdev ? spdk_bdev_get_name(core->base.bdev) : "" );
//	spdk_json_write_named_string(w, "cache_name", vbdev_ocf_core_get_cache(core)->name);
//	spdk_json_write_object_end(w);
//}

/* RPC entry point. */
void
vbdev_ocf_get_bdevs(const char *name, vbdev_ocf_get_bdevs_cb rpc_cb_fn, void *rpc_cb_arg1, void *rpc_cb_arg2)
{
	//comp
	//struct spdk_json_write_ctx *w = rpc_cb_arg1;
	//struct vbdev_ocf_cache *cache;
	//struct vbdev_ocf_core *core;
	//bool found = false;

	//if (name) {
	//	vbdev_ocf_foreach_cache(cache) {
	//		vbdev_ocf_foreach_core_in_cache(core, cache) {
	//			if (strcmp(name, core->name)) {
	//				continue;
	//			}
	//			found = true;

	//			// dump_info_json() instead?
	//			_write_core_info(w, core);
	//			break;
	//		}
	//		if (found) {
	//			break;
	//		}
	//		if (strcmp(name, cache->name)) {
	//			continue;
	//		}

	//		_write_cache_info_begin(w, cache);
	//		_write_cache_info_end(w, cache);
	//		break;
	//	}
	//} else {
	//	vbdev_ocf_foreach_cache(cache) {
	//		_write_cache_info_begin(w, cache);
	//		spdk_json_write_named_array_begin(w, "cores");
	//		vbdev_ocf_foreach_core_in_cache(core, cache) {
	//			_write_core_info(w, core);
	//		}
	//		spdk_json_write_array_end(w);
	//		_write_cache_info_end(w, cache);
	//	}
	//}

	//rpc_cb_fn(rpc_cb_arg1, rpc_cb_arg2);
}

SPDK_LOG_REGISTER_COMPONENT(vbdev_ocf)
