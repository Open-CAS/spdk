/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "vbdev_ocf.h"
#include "stats.h"
#include "utils.h"
#include "spdk/log.h"
#include "spdk/rpc.h"
#include "spdk/string.h"

/* Structure to hold the parameters for this RPC method. */
struct rpc_bdev_ocf_create {
	char *name;			/* main vbdev */
	char *mode;			/* OCF mode (choose one) */
	uint64_t cache_line_size;	/* OCF cache line size */
	char *cache_bdev_name;		/* sub bdev */
	char *core_bdev_name;		/* sub bdev */
	bool create;			/* Create cache instance */
	bool force;			/* Force create cache instance */
	char* cpu_mask;		/* CPU mask for background operations */
};

static void
free_rpc_bdev_ocf_create(struct rpc_bdev_ocf_create *r)
{
	free(r->name);
	free(r->core_bdev_name);
	free(r->cache_bdev_name);
	free(r->mode);
	free(r->cpu_mask);
}

/* Structure to decode the input parameters for this RPC method. */
static const struct spdk_json_object_decoder rpc_bdev_ocf_create_decoders[] = {
	{"name", offsetof(struct rpc_bdev_ocf_create, name), spdk_json_decode_string},
	{"mode", offsetof(struct rpc_bdev_ocf_create, mode), spdk_json_decode_string},
	{"cache_line_size", offsetof(struct rpc_bdev_ocf_create, cache_line_size), spdk_json_decode_uint64, true},
	{"cache_bdev_name", offsetof(struct rpc_bdev_ocf_create, cache_bdev_name), spdk_json_decode_string},
	{"core_bdev_name", offsetof(struct rpc_bdev_ocf_create, core_bdev_name), spdk_json_decode_string},
	{"create", offsetof(struct rpc_bdev_ocf_create, create), spdk_json_decode_bool, true},
	{"force", offsetof(struct rpc_bdev_ocf_create, force), spdk_json_decode_bool, true},
	{"cpu_mask", offsetof(struct rpc_bdev_ocf_create, cpu_mask), spdk_json_decode_string, true},
};

static void
construct_cb(int status, struct vbdev_ocf *vbdev, void *cb_arg)
{
	struct spdk_jsonrpc_request *request = cb_arg;
	struct spdk_json_write_ctx *w;

	if (status) {
		spdk_jsonrpc_send_error_response_fmt(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						     "Could not create OCF vbdev: %d",
						     status);
	} else {
		w = spdk_jsonrpc_begin_result(request);
		spdk_json_write_string(w, vbdev->name);
		spdk_jsonrpc_end_result(request, w);
	}
}

static void
rpc_bdev_ocf_create(struct spdk_jsonrpc_request *request,
		    const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_create req = {NULL};
	int ret;

	ret = spdk_json_decode_object(params, rpc_bdev_ocf_create_decoders,
				      SPDK_COUNTOF(rpc_bdev_ocf_create_decoders),
				      &req);
	if (ret) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		free_rpc_bdev_ocf_create(&req);
		return;
	}

	if (req.force && !req.create) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameter - can't use force flag without create flag");
		free_rpc_bdev_ocf_create(&req);
		return;
	}

	vbdev_ocf_construct(req.name, req.mode, req.cache_line_size, req.cache_bdev_name,
			    req.core_bdev_name, req.create, req.force, req.cpu_mask, construct_cb, request);
	free_rpc_bdev_ocf_create(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_create", rpc_bdev_ocf_create, SPDK_RPC_RUNTIME)
SPDK_RPC_REGISTER_ALIAS_DEPRECATED(bdev_ocf_create, construct_ocf_bdev)

/* Structure to hold the parameters for this RPC method. */
struct rpc_bdev_ocf_delete {
	char *name;             /* main vbdev name */
};

static void
free_rpc_bdev_ocf_delete(struct rpc_bdev_ocf_delete *r)
{
	free(r->name);
}

/* Structure to decode the input parameters for this RPC method. */
static const struct spdk_json_object_decoder rpc_bdev_ocf_delete_decoders[] = {
	{"name", offsetof(struct rpc_bdev_ocf_delete, name), spdk_json_decode_string},
};

static void
delete_cb(void *cb_arg, int status)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (status) {
		spdk_jsonrpc_send_error_response_fmt(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						     "Could not delete OCF vbdev: %d",
						     status);
	} else {
		spdk_jsonrpc_send_bool_response(request, true);
	}
}

static void
rpc_bdev_ocf_delete(struct spdk_jsonrpc_request *request,
		    const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_delete req = {NULL};
	struct vbdev_ocf *vbdev;
	int status;

	status = spdk_json_decode_object(params, rpc_bdev_ocf_delete_decoders,
					 SPDK_COUNTOF(rpc_bdev_ocf_delete_decoders),
					 &req);
	if (status) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto end;
	}

	vbdev = vbdev_ocf_get_by_name(req.name);
	if (vbdev == NULL) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 spdk_strerror(ENODEV));
		goto end;
	}

	status = vbdev_ocf_delete_clean(vbdev, delete_cb, request);
	if (status) {
		spdk_jsonrpc_send_error_response_fmt(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						     "Could not delete OCF vbdev: %s",
						     spdk_strerror(-status));
		goto end;
	}

end:
	free_rpc_bdev_ocf_delete(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_delete", rpc_bdev_ocf_delete, SPDK_RPC_RUNTIME)
SPDK_RPC_REGISTER_ALIAS_DEPRECATED(bdev_ocf_delete, delete_ocf_bdev)

/* Structure to hold the parameters for this RPC method. */
struct rpc_bdev_ocf_get_stats {
	char *name;             /* main vbdev name */
};

static void
free_rpc_bdev_ocf_get_stats(struct rpc_bdev_ocf_get_stats *r)
{
	free(r->name);
}

/* Structure to decode the input parameters for this RPC method. */
static const struct spdk_json_object_decoder rpc_bdev_ocf_get_stats_decoders[] = {
	{"name", offsetof(struct rpc_bdev_ocf_get_stats, name), spdk_json_decode_string},
};

struct get_ocf_stats_ctx {
	struct spdk_jsonrpc_request *request;
	char *core_name;
};

static void
rpc_bdev_ocf_get_stats_cmpl(ocf_cache_t cache, void *priv, int error)
{
	struct get_ocf_stats_ctx *ctx = (struct get_ocf_stats_ctx *) priv;
	struct spdk_json_write_ctx *w;
	struct vbdev_ocf_stats stats;

	if (error) {
		goto end;
	}

	error = vbdev_ocf_stats_get(cache, ctx->core_name, &stats);

	ocf_mngt_cache_read_unlock(cache);

	if (error) {
		goto end;
	}

	w = spdk_jsonrpc_begin_result(ctx->request);
	vbdev_ocf_stats_write_json(w, &stats);
	spdk_jsonrpc_end_result(ctx->request, w);

end:
	if (error) {
		spdk_jsonrpc_send_error_response_fmt(ctx->request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						     "Could not get stats: %s",
						     spdk_strerror(-error));
	}
	free(ctx);
}

static void
rpc_bdev_ocf_get_stats(struct spdk_jsonrpc_request *request,
		       const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_get_stats req = {NULL};
	struct vbdev_ocf *vbdev;
	struct get_ocf_stats_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Not enough memory to process request");
		goto end;
	}

	if (spdk_json_decode_object(params, rpc_bdev_ocf_get_stats_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_get_stats_decoders),
				    &req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		free(ctx);
		goto end;
	}

	vbdev = vbdev_ocf_get_by_name(req.name);
	if (vbdev == NULL) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 spdk_strerror(ENODEV));
		free(ctx);
		goto end;
	}

	ctx->core_name = vbdev->core.name;
	ctx->request = request;
	ocf_mngt_cache_read_lock(vbdev->ocf_cache, rpc_bdev_ocf_get_stats_cmpl, ctx);

end:
	free_rpc_bdev_ocf_get_stats(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_get_stats", rpc_bdev_ocf_get_stats, SPDK_RPC_RUNTIME)
SPDK_RPC_REGISTER_ALIAS_DEPRECATED(bdev_ocf_get_stats, get_ocf_stats)

/* Structure to hold the parameters for this RPC method. */
struct rpc_bdev_ocf_get_bdevs {
	char *name;
};

static void
free_rpc_bdev_ocf_get_bdevs(struct rpc_bdev_ocf_get_bdevs *r)
{
	free(r->name);
}

/* Structure to decode the input parameters for this RPC method. */
static const struct spdk_json_object_decoder rpc_bdev_ocf_get_bdevs_decoders[] = {
	{"name", offsetof(struct rpc_bdev_ocf_get_bdevs, name), spdk_json_decode_string, true},
};

struct bdev_get_bdevs_ctx {
	char *name;
	struct spdk_json_write_ctx *w;
};

static void
ocf_json_write_cleaning_params(struct spdk_json_write_ctx *w, ocf_cache_t cache) {
	ocf_cleaning_t cleaning_policy;
	uint32_t cleaning_wake_up, cleaning_staleness_time,
			 cleaning_flush_max_buffers, cleaning_activity_threshold;
	int rc;

	rc = ocf_mngt_cache_cleaning_get_policy(cache, &cleaning_policy);
	if (rc)
		SPDK_ERRLOG("Could not get OCF cleaning policy: %d\n", rc);
	else {
		spdk_json_write_named_string(w, "policy", ocf_get_cleaning_policy_name(cleaning_policy));
	}

	if (cleaning_policy == ocf_cleaning_alru) {
		rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_alru,
				ocf_alru_wake_up_time, &cleaning_wake_up);
		if (rc)
			SPDK_ERRLOG("Could not get OCF cleaning wake up time: %d\n", rc);
		else
			spdk_json_write_named_uint32(w, "wake_up", cleaning_wake_up);

		rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_alru,
				ocf_alru_stale_buffer_time, &cleaning_staleness_time);
		if (rc)
			SPDK_ERRLOG("Could not get OCF cleaning staleness time: %d\n", rc);
		else
			spdk_json_write_named_uint32(w, "staleness_time", cleaning_staleness_time);

		rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_alru,
				ocf_alru_flush_max_buffers, &cleaning_flush_max_buffers);
		if (rc)
			SPDK_ERRLOG("Could not get OCF cleaning flush max buffers: %d\n", rc);
		else
			spdk_json_write_named_uint32(w, "flush_max_buffers", cleaning_flush_max_buffers);

		rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_alru,
				ocf_alru_activity_threshold, &cleaning_activity_threshold);
		if (rc)
			SPDK_ERRLOG("Could not get OCF cleaning activity threshold: %d\n", rc);
		else
			spdk_json_write_named_uint32(w, "activity_threshold", cleaning_activity_threshold);

	} else if (cleaning_policy == ocf_cleaning_acp) {
		rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_acp,
				ocf_acp_wake_up_time, &cleaning_wake_up);
		if (rc)
			SPDK_ERRLOG("Could not get OCF cleaning wake up time: %d\n", rc);
		else
			spdk_json_write_named_uint32(w, "wake_up", cleaning_wake_up);

		rc = ocf_mngt_cache_cleaning_get_param(cache, ocf_cleaning_acp,
				ocf_acp_flush_max_buffers, &cleaning_flush_max_buffers);
		if (rc)
			SPDK_ERRLOG("Could not get OCF cleaning flush max buffers: %d\n", rc);
		else
			spdk_json_write_named_uint32(w, "flush_max_buffers", cleaning_flush_max_buffers);

	}
}

static void
ocf_json_write_seqcutoff_params(struct spdk_json_write_ctx *w, ocf_core_t core) {
	ocf_seq_cutoff_policy seqcutoff_policy;
	uint32_t seqcutoff_threshold, seqcutoff_promotion_count;
	int rc;

	rc = ocf_mngt_core_get_seq_cutoff_policy(core, &seqcutoff_policy);
	if (rc)
		SPDK_ERRLOG("Could not get OCF sequential cutoff policy: %d\n", rc);
	else
		spdk_json_write_named_string(w, "policy", ocf_get_seqcutoff_policy_name(seqcutoff_policy));

	rc = ocf_mngt_core_get_seq_cutoff_threshold(core, &seqcutoff_threshold);
	if (rc)
		SPDK_ERRLOG("Could not get OCF sequential cutoff threshold: %d\n", rc);
	else
		spdk_json_write_named_uint32(w, "threshold", seqcutoff_threshold);

	rc = ocf_mngt_core_get_seq_cutoff_promotion_count(core, &seqcutoff_promotion_count);
	if (rc)
		SPDK_ERRLOG("Could not get OCF sequential cutoff promotion count: %d\n", rc);
	else
		spdk_json_write_named_uint32(w, "promotion_count", seqcutoff_promotion_count);
}

static void
bdev_get_bdevs_fn(struct vbdev_ocf *vbdev, void *ctx)
{
	struct bdev_get_bdevs_ctx *cctx = ctx;
	struct spdk_json_write_ctx *w = cctx->w;
	const char *cache_mode_name;
	uint32_t cache_line_size;
	ocf_cache_t cache;
	ocf_core_t core;

	if (cctx->name != NULL &&
	    strcmp(vbdev->name, cctx->name) &&
	    strcmp(vbdev->cache.name, cctx->name) &&
	    strcmp(vbdev->core.name, cctx->name)) {
		return;
	}

	cache = vbdev->ocf_cache;
	core = vbdev->ocf_core;
	cache_mode_name = ocf_get_cache_modename(ocf_cache_get_mode(cache));
	cache_line_size = ocf_get_cache_line_size(cache);

	spdk_json_write_object_begin(w);
	spdk_json_write_named_string(w, "name", vbdev->name);
	spdk_json_write_named_bool(w, "started", vbdev->state.started);

	spdk_json_write_named_object_begin(w, "cache");
	spdk_json_write_named_string(w, "name", vbdev->cache.name);
	spdk_json_write_named_bool(w, "attached", vbdev->cache.attached);
	spdk_json_write_named_string(w, "cache_mode", cache_mode_name);
	spdk_json_write_named_uint32(w, "cache_line_size", cache_line_size);

	spdk_json_write_named_object_begin(w, "cleaning");
	ocf_json_write_cleaning_params(w, cache);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "core");
	spdk_json_write_named_string(w, "name", vbdev->core.name);
	spdk_json_write_named_bool(w, "attached", vbdev->core.attached);

	spdk_json_write_named_object_begin(w, "sequential_cutoff");
	ocf_json_write_seqcutoff_params(w, core);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);
}

static void
rpc_bdev_ocf_get_bdevs(struct spdk_jsonrpc_request *request,
		       const struct spdk_json_val *params)
{
	struct spdk_json_write_ctx *w;
	struct rpc_bdev_ocf_get_bdevs req = {NULL};
	struct bdev_get_bdevs_ctx cctx;

	if (params && spdk_json_decode_object(params, rpc_bdev_ocf_get_bdevs_decoders,
					      SPDK_COUNTOF(rpc_bdev_ocf_get_bdevs_decoders),
					      &req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto end;
	}

	if (req.name) {
		if (!(vbdev_ocf_get_by_name(req.name) || vbdev_ocf_get_base_by_name(req.name))) {
			spdk_jsonrpc_send_error_response(request,
							 SPDK_JSONRPC_ERROR_INVALID_PARAMS,
							 spdk_strerror(ENODEV));
			goto end;
		}
	}

	w = spdk_jsonrpc_begin_result(request);

	cctx.name    = req.name;
	cctx.w       = w;

	spdk_json_write_array_begin(w);
	vbdev_ocf_foreach(bdev_get_bdevs_fn, &cctx);
	spdk_json_write_array_end(w);
	spdk_jsonrpc_end_result(request, w);

end:
	free_rpc_bdev_ocf_get_bdevs(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_get_bdevs", rpc_bdev_ocf_get_bdevs, SPDK_RPC_RUNTIME)
SPDK_RPC_REGISTER_ALIAS_DEPRECATED(bdev_ocf_get_bdevs, get_ocf_bdevs)

/* Structure to hold the parameters for this RPC method. */
struct rpc_bdev_ocf_set_cache_mode {
	char *name;			/* main vbdev name */
	char *mode;			/* OCF cache mode to switch to */
};

static void
free_rpc_bdev_ocf_set_cache_mode(struct rpc_bdev_ocf_set_cache_mode *r)
{
	free(r->name);
	free(r->mode);
}

/* Structure to decode the input parameters for this RPC method. */
static const struct spdk_json_object_decoder rpc_bdev_ocf_set_cache_mode_decoders[] = {
	{"name", offsetof(struct rpc_bdev_ocf_set_cache_mode, name), spdk_json_decode_string},
	{"mode", offsetof(struct rpc_bdev_ocf_set_cache_mode, mode), spdk_json_decode_string},
};

static void
cache_mode_cb(int status, struct vbdev_ocf *vbdev, void *cb_arg)
{
	struct spdk_jsonrpc_request *request = cb_arg;
	struct spdk_json_write_ctx *w;

	if (status) {
		spdk_jsonrpc_send_error_response_fmt(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						     "Could not change OCF vbdev cache mode: %d",
						     status);
	} else {
		w = spdk_jsonrpc_begin_result(request);
		spdk_json_write_string(w, ocf_get_cache_modename(
					       ocf_cache_get_mode(vbdev->ocf_cache)));
		spdk_jsonrpc_end_result(request, w);
	}
}

static void
rpc_bdev_ocf_set_cache_mode(struct spdk_jsonrpc_request *request,
			    const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_set_cache_mode req = {NULL};
	struct vbdev_ocf *vbdev;
	int status;

	status = spdk_json_decode_object(params, rpc_bdev_ocf_set_cache_mode_decoders,
					 SPDK_COUNTOF(rpc_bdev_ocf_set_cache_mode_decoders),
					 &req);
	if (status) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto end;
	}

	vbdev = vbdev_ocf_get_by_name(req.name);
	if (vbdev == NULL) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 spdk_strerror(ENODEV));
		goto end;
	}

	vbdev_ocf_set_cache_mode(vbdev, req.mode, cache_mode_cb, request);

end:
	free_rpc_bdev_ocf_set_cache_mode(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_set_cache_mode", rpc_bdev_ocf_set_cache_mode, SPDK_RPC_RUNTIME)
