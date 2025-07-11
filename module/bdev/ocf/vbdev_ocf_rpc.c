/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#include "vbdev_ocf.h"
#include "spdk/rpc.h"
#include "spdk/string.h"

struct rpc_bdev_ocf_start_cache {
	char *cache_name;
	char *base_name;
	char *cache_mode;
	uint8_t cache_line_size;
	bool no_load;
};

static void
free_rpc_bdev_ocf_start_cache(struct rpc_bdev_ocf_start_cache *r)
{
	free(r->cache_name);
	free(r->base_name);
	free(r->cache_mode);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_start_cache_decoders[] = {
	{"cache_name", offsetof(struct rpc_bdev_ocf_start_cache, cache_name), spdk_json_decode_string},
	{"base_name", offsetof(struct rpc_bdev_ocf_start_cache, base_name), spdk_json_decode_string},
	{"cache_mode", offsetof(struct rpc_bdev_ocf_start_cache, cache_mode), spdk_json_decode_string, true},
	{"cache_line_size", offsetof(struct rpc_bdev_ocf_start_cache, cache_line_size), spdk_json_decode_uint8, true},
	{"no_load", offsetof(struct rpc_bdev_ocf_start_cache, no_load), spdk_json_decode_bool, true},
};

static void
rpc_bdev_ocf_start_cache_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;
	struct spdk_json_write_ctx *w;

	if (error && error != -ENODEV) {
		spdk_jsonrpc_send_error_response_fmt(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						     "Could not start OCF cache: %s",
						     spdk_strerror(-error));
		return;
	}

	w = spdk_jsonrpc_begin_result(request);
	spdk_json_write_string(w, bdev_name);
	spdk_jsonrpc_end_result(request, w);
}

static void
rpc_bdev_ocf_start_cache(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_start_cache req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_start_cache_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_start_cache_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_cache_start(req.cache_name, req.base_name, req.cache_mode, req.cache_line_size,
			      req.no_load, rpc_bdev_ocf_start_cache_cb, request);

cleanup:
	free_rpc_bdev_ocf_start_cache(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_start_cache", rpc_bdev_ocf_start_cache, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_stop_cache {
	char *cache_name;
};

static void
free_rpc_bdev_ocf_stop_cache(struct rpc_bdev_ocf_stop_cache *r)
{
	free(r->cache_name);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_stop_cache_decoders[] = {
	{"cache_name", offsetof(struct rpc_bdev_ocf_stop_cache, cache_name), spdk_json_decode_string},
};

static void
rpc_bdev_ocf_stop_cache_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (error) {
		spdk_jsonrpc_send_error_response(request, error, spdk_strerror(-error));
		return;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}

static void
rpc_bdev_ocf_stop_cache(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_stop_cache req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_stop_cache_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_stop_cache_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_cache_stop(req.cache_name, rpc_bdev_ocf_stop_cache_cb, request);

cleanup:
	free_rpc_bdev_ocf_stop_cache(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_stop_cache", rpc_bdev_ocf_stop_cache, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_detach_cache {
	char *cache_name;
};

static void
free_rpc_bdev_ocf_detach_cache(struct rpc_bdev_ocf_detach_cache *r)
{
	free(r->cache_name);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_detach_cache_decoders[] = {
	{"cache_name", offsetof(struct rpc_bdev_ocf_detach_cache, cache_name), spdk_json_decode_string},
};

static void
rpc_bdev_ocf_detach_cache_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (error) {
		spdk_jsonrpc_send_error_response_fmt(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						     "Could not detach OCF cache: %s",
						     spdk_strerror(-error));
		return;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}

static void
rpc_bdev_ocf_detach_cache(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_detach_cache req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_detach_cache_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_detach_cache_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_cache_detach(req.cache_name, rpc_bdev_ocf_detach_cache_cb, request);

cleanup:
	free_rpc_bdev_ocf_detach_cache(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_detach_cache", rpc_bdev_ocf_detach_cache, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_attach_cache {
	char *cache_name;
	char *base_name;
	bool force;
};

static void
free_rpc_bdev_ocf_attach_cache(struct rpc_bdev_ocf_attach_cache *r)
{
	free(r->cache_name);
	free(r->base_name);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_attach_cache_decoders[] = {
	{"cache_name", offsetof(struct rpc_bdev_ocf_attach_cache, cache_name), spdk_json_decode_string},
	{"base_name", offsetof(struct rpc_bdev_ocf_attach_cache, base_name), spdk_json_decode_string},
	{"force", offsetof(struct rpc_bdev_ocf_attach_cache, force), spdk_json_decode_bool, true},
};

static void
rpc_bdev_ocf_attach_cache_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (error && error != -ENODEV) {
		spdk_jsonrpc_send_error_response_fmt(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						     "Could not attach OCF cache: %s",
						     spdk_strerror(-error));
		return;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}

static void
rpc_bdev_ocf_attach_cache(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_attach_cache req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_attach_cache_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_attach_cache_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_cache_attach(req.cache_name, req.base_name, req.force,
			       rpc_bdev_ocf_attach_cache_cb, request);

cleanup:
	free_rpc_bdev_ocf_attach_cache(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_attach_cache", rpc_bdev_ocf_attach_cache, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_add_core {
	char *core_name;
	char *base_name;
	char *cache_name;
};

static void
free_rpc_bdev_ocf_add_core(struct rpc_bdev_ocf_add_core *r)
{
	free(r->core_name);
	free(r->base_name);
	free(r->cache_name);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_add_core_decoders[] = {
	{"core_name", offsetof(struct rpc_bdev_ocf_add_core, core_name), spdk_json_decode_string},
	{"base_name", offsetof(struct rpc_bdev_ocf_add_core, base_name), spdk_json_decode_string},
	{"cache_name", offsetof(struct rpc_bdev_ocf_add_core, cache_name), spdk_json_decode_string},
};

static void
rpc_bdev_ocf_add_core_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;
	struct spdk_json_write_ctx *w;

	if (error && error != -ENODEV) {
		spdk_jsonrpc_send_error_response_fmt(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						     "Could not add core to OCF cache: %s",
						     spdk_strerror(-error));
		return;
	}

	w = spdk_jsonrpc_begin_result(request);
	spdk_json_write_string(w, bdev_name);
	spdk_jsonrpc_end_result(request, w);
}

static void
rpc_bdev_ocf_add_core(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_add_core req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_add_core_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_add_core_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_core_add(req.core_name, req.base_name, req.cache_name, rpc_bdev_ocf_add_core_cb, request);

cleanup:
	free_rpc_bdev_ocf_add_core(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_add_core", rpc_bdev_ocf_add_core, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_remove_core {
	char *core_name;
	char *cache_name;
};

static void
free_rpc_bdev_ocf_remove_core(struct rpc_bdev_ocf_remove_core *r)
{
	free(r->core_name);
	free(r->cache_name);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_remove_core_decoders[] = {
	{"core_name", offsetof(struct rpc_bdev_ocf_remove_core, core_name), spdk_json_decode_string},
	{"cache_name", offsetof(struct rpc_bdev_ocf_remove_core, cache_name), spdk_json_decode_string},
};

static void
rpc_bdev_ocf_remove_core_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (error) {
		spdk_jsonrpc_send_error_response(request, error, spdk_strerror(-error));
		return;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}

static void
rpc_bdev_ocf_remove_core(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_remove_core req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_remove_core_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_remove_core_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_core_remove(req.core_name, req.cache_name, rpc_bdev_ocf_remove_core_cb, request);

cleanup:
	free_rpc_bdev_ocf_remove_core(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_remove_core", rpc_bdev_ocf_remove_core, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_set_cachemode {
	char *cache_name;
	char *cache_mode;
};

static void
free_rpc_bdev_ocf_set_cachemode(struct rpc_bdev_ocf_set_cachemode *r)
{
	free(r->cache_name);
	free(r->cache_mode);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_set_cachemode_decoders[] = {
	{"cache_name", offsetof(struct rpc_bdev_ocf_set_cachemode, cache_name), spdk_json_decode_string},
	{"cache_mode", offsetof(struct rpc_bdev_ocf_set_cachemode, cache_mode), spdk_json_decode_string},
};

static void
rpc_bdev_ocf_set_cachemode_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (error) {
		spdk_jsonrpc_send_error_response(request, error, spdk_strerror(-error));
		return;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}

static void
rpc_bdev_ocf_set_cachemode(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_set_cachemode req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_set_cachemode_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_set_cachemode_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_set_cachemode(req.cache_name, req.cache_mode, rpc_bdev_ocf_set_cachemode_cb, request);

cleanup:
	free_rpc_bdev_ocf_set_cachemode(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_set_cachemode", rpc_bdev_ocf_set_cachemode, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_set_promotion {
	char *cache_name;
	char *policy;
	int32_t nhit_insertion_threshold;
	int32_t nhit_trigger_threshold;
};

static void
free_rpc_bdev_ocf_set_promotion(struct rpc_bdev_ocf_set_promotion *r)
{
	free(r->cache_name);
	free(r->policy);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_set_promotion_decoders[] = {
	{"cache_name", offsetof(struct rpc_bdev_ocf_set_promotion, cache_name), spdk_json_decode_string},
	{"policy", offsetof(struct rpc_bdev_ocf_set_promotion, policy), spdk_json_decode_string, true},
	{"nhit_insertion_threshold", offsetof(struct rpc_bdev_ocf_set_promotion, nhit_insertion_threshold), spdk_json_decode_int32, true},
	{"nhit_trigger_threshold", offsetof(struct rpc_bdev_ocf_set_promotion, nhit_trigger_threshold), spdk_json_decode_int32, true},
};

static void
rpc_bdev_ocf_set_promotion_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (error) {
		spdk_jsonrpc_send_error_response(request, error, spdk_strerror(-error));
		return;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}

static void
rpc_bdev_ocf_set_promotion(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_set_promotion req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_set_promotion_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_set_promotion_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_set_promotion(req.cache_name, req.policy, req.nhit_insertion_threshold,
				req.nhit_trigger_threshold, rpc_bdev_ocf_set_promotion_cb, request);

cleanup:
	free_rpc_bdev_ocf_set_promotion(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_set_promotion", rpc_bdev_ocf_set_promotion, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_set_cleaning {
	char *cache_name;
	char *policy;
	int32_t acp_wake_up_time;
	int32_t acp_flush_max_buffers;
	int32_t alru_wake_up_time;
	int32_t alru_flush_max_buffers;
	int32_t alru_staleness_time;
	int32_t alru_activity_threshold;
	int32_t alru_max_dirty_ratio;
};

static void
free_rpc_bdev_ocf_set_cleaning(struct rpc_bdev_ocf_set_cleaning *r)
{
	free(r->cache_name);
	free(r->policy);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_set_cleaning_decoders[] = {
	{"cache_name", offsetof(struct rpc_bdev_ocf_set_cleaning, cache_name), spdk_json_decode_string},
	{"policy", offsetof(struct rpc_bdev_ocf_set_cleaning, policy), spdk_json_decode_string, true},
	{"acp_wake_up_time", offsetof(struct rpc_bdev_ocf_set_cleaning, acp_wake_up_time), spdk_json_decode_int32, true},
	{"acp_flush_max_buffers", offsetof(struct rpc_bdev_ocf_set_cleaning, acp_flush_max_buffers), spdk_json_decode_int32, true},
	{"alru_wake_up_time", offsetof(struct rpc_bdev_ocf_set_cleaning, alru_wake_up_time), spdk_json_decode_int32, true},
	{"alru_flush_max_buffers", offsetof(struct rpc_bdev_ocf_set_cleaning, alru_flush_max_buffers), spdk_json_decode_int32, true},
	{"alru_staleness_time", offsetof(struct rpc_bdev_ocf_set_cleaning, alru_staleness_time), spdk_json_decode_int32, true},
	{"alru_activity_threshold", offsetof(struct rpc_bdev_ocf_set_cleaning, alru_activity_threshold), spdk_json_decode_int32, true},
	{"alru_max_dirty_ratio", offsetof(struct rpc_bdev_ocf_set_cleaning, alru_max_dirty_ratio), spdk_json_decode_int32, true},
};

static void
rpc_bdev_ocf_set_cleaning_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (error) {
		spdk_jsonrpc_send_error_response(request, error, spdk_strerror(-error));
		return;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}

static void
rpc_bdev_ocf_set_cleaning(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_set_cleaning req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_set_cleaning_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_set_cleaning_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_set_cleaning(req.cache_name, req.policy, req.acp_wake_up_time,
			       req.acp_flush_max_buffers, req.alru_wake_up_time,
			       req.alru_flush_max_buffers, req.alru_staleness_time,
			       req.alru_activity_threshold, req.alru_max_dirty_ratio,
			       rpc_bdev_ocf_set_cleaning_cb, request);

cleanup:
	free_rpc_bdev_ocf_set_cleaning(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_set_cleaning", rpc_bdev_ocf_set_cleaning, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_set_seqcutoff {
	char *bdev_name;
	char *policy;
	int32_t threshold;
	int32_t promotion_count;
	int32_t promote_on_threshold;
};

static void
free_rpc_bdev_ocf_set_seqcutoff(struct rpc_bdev_ocf_set_seqcutoff *r)
{
	free(r->bdev_name);
	free(r->policy);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_set_seqcutoff_decoders[] = {
	{"bdev_name", offsetof(struct rpc_bdev_ocf_set_seqcutoff, bdev_name), spdk_json_decode_string},
	{"policy", offsetof(struct rpc_bdev_ocf_set_seqcutoff, policy), spdk_json_decode_string, true},
	{"threshold", offsetof(struct rpc_bdev_ocf_set_seqcutoff, threshold), spdk_json_decode_int32, true},
	{"promotion_count", offsetof(struct rpc_bdev_ocf_set_seqcutoff, promotion_count), spdk_json_decode_int32, true},
	{"promote_on_threshold", offsetof(struct rpc_bdev_ocf_set_seqcutoff, promote_on_threshold), spdk_json_decode_int32, true},
};

static void
rpc_bdev_ocf_set_seqcutoff_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (error) {
		spdk_jsonrpc_send_error_response(request, error, spdk_strerror(-error));
		return;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}

static void
rpc_bdev_ocf_set_seqcutoff(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_set_seqcutoff req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_set_seqcutoff_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_set_seqcutoff_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_set_seqcutoff(req.bdev_name, req.policy, req.threshold, req.promotion_count,
				req.promote_on_threshold, rpc_bdev_ocf_set_seqcutoff_cb, request);

cleanup:
	free_rpc_bdev_ocf_set_seqcutoff(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_set_seqcutoff", rpc_bdev_ocf_set_seqcutoff, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_flush_start {
	char *bdev_name;
};

static void
free_rpc_bdev_ocf_flush_start(struct rpc_bdev_ocf_flush_start *r)
{
	free(r->bdev_name);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_flush_start_decoders[] = {
	{"bdev_name", offsetof(struct rpc_bdev_ocf_flush_start, bdev_name), spdk_json_decode_string},
};

static void
rpc_bdev_ocf_flush_start_cb(const char *bdev_name, void *cb_arg, int error)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (error) {
		spdk_jsonrpc_send_error_response(request, error, spdk_strerror(-error));
		return;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}

static void
rpc_bdev_ocf_flush_start(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_flush_start req = {};

	if (spdk_json_decode_object(params, rpc_bdev_ocf_flush_start_decoders,
				    SPDK_COUNTOF(rpc_bdev_ocf_flush_start_decoders),
				    &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	vbdev_ocf_flush_start(req.bdev_name, rpc_bdev_ocf_flush_start_cb, request);

cleanup:
	free_rpc_bdev_ocf_flush_start(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_flush_start", rpc_bdev_ocf_flush_start, SPDK_RPC_RUNTIME)

static void
rpc_bdev_ocf_get_stats(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
}
SPDK_RPC_REGISTER("bdev_ocf_get_stats", rpc_bdev_ocf_get_stats, SPDK_RPC_RUNTIME)

static void
rpc_bdev_ocf_reset_stats(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
}
SPDK_RPC_REGISTER("bdev_ocf_reset_stats", rpc_bdev_ocf_reset_stats, SPDK_RPC_RUNTIME)

struct rpc_bdev_ocf_get_bdevs {
	char *name;
};

static void
free_rpc_bdev_ocf_get_bdevs(struct rpc_bdev_ocf_get_bdevs *r)
{
	free(r->name);
}

static const struct spdk_json_object_decoder rpc_bdev_ocf_get_bdevs_decoders[] = {
	{"name", offsetof(struct rpc_bdev_ocf_get_bdevs, name), spdk_json_decode_string, true},
};

static void
rpc_bdev_ocf_get_bdevs_cb(void *cb_arg1, void *cb_arg2)
{
	struct spdk_json_write_ctx *w = cb_arg1;
	struct spdk_jsonrpc_request *request = cb_arg2;

	spdk_json_write_object_end(w);
	spdk_jsonrpc_end_result(request, w);
}

static void
rpc_bdev_ocf_get_bdevs(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct rpc_bdev_ocf_get_bdevs req = {};
	struct spdk_json_write_ctx *w;

	if (params && spdk_json_decode_object(params, rpc_bdev_ocf_get_bdevs_decoders,
					      SPDK_COUNTOF(rpc_bdev_ocf_get_bdevs_decoders),
					      &req)) {
		SPDK_DEBUGLOG(vbdev_ocf_rpc, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
						 "Invalid parameters");
		goto cleanup;
	}

	w = spdk_jsonrpc_begin_result(request);
	spdk_json_write_object_begin(w);

	vbdev_ocf_get_bdevs(params ? req.name : NULL, rpc_bdev_ocf_get_bdevs_cb, w, request);

cleanup:
	free_rpc_bdev_ocf_get_bdevs(&req);
}
SPDK_RPC_REGISTER("bdev_ocf_get_bdevs", rpc_bdev_ocf_get_bdevs, SPDK_RPC_RUNTIME)

SPDK_LOG_REGISTER_COMPONENT(vbdev_ocf_rpc)
