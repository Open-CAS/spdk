/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#include "stats.h"

int
vbdev_ocf_stats_cache_get(ocf_cache_t cache, struct vbdev_ocf_stats *stats)
{
	return ocf_stats_collect_cache(cache, &stats->usage, &stats->req, &stats->blocks, &stats->errors);
}

int
vbdev_ocf_stats_core_get(ocf_core_t core, struct vbdev_ocf_stats *stats)
{
	return ocf_stats_collect_core(core, &stats->usage, &stats->req, &stats->blocks, &stats->errors);
}

int
vbdev_ocf_stats_cache_reset(ocf_cache_t cache)
{
	return ocf_core_stats_initialize_all(cache);
}

int
vbdev_ocf_stats_core_reset(ocf_core_t core)
{
	ocf_core_stats_initialize(core);
	return 0;
}

#define WJSON_STAT(w, stats, group, field, units) \
	spdk_json_write_named_object_begin(w, #field); \
	spdk_json_write_named_uint64(w, "count", stats->group.field.value); \
	spdk_json_write_named_string_fmt(w, "percentage", "%lu.%lu", \
		stats->group.field.fraction / 100, stats->group.field.fraction % 100); \
	spdk_json_write_named_string(w, "units", units); \
	spdk_json_write_object_end(w);

void
vbdev_ocf_stats_write_json(struct spdk_json_write_ctx *w, struct vbdev_ocf_stats *stats)
{
	spdk_json_write_named_object_begin(w, "usage");
	WJSON_STAT(w, stats, usage, occupancy, "4KiB blocks");
	WJSON_STAT(w, stats, usage, free, "4KiB blocks");
	WJSON_STAT(w, stats, usage, clean, "4KiB blocks");
	WJSON_STAT(w, stats, usage, dirty, "4KiB blocks");
	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "requests");
	WJSON_STAT(w, stats, req, rd_hits, "Requests");
	WJSON_STAT(w, stats, req, rd_partial_misses, "Requests");
	WJSON_STAT(w, stats, req, rd_full_misses, "Requests");
	WJSON_STAT(w, stats, req, rd_total, "Requests");
	WJSON_STAT(w, stats, req, wr_hits, "Requests");
	WJSON_STAT(w, stats, req, wr_partial_misses, "Requests");
	WJSON_STAT(w, stats, req, wr_full_misses, "Requests");
	WJSON_STAT(w, stats, req, wr_total, "Requests");
	WJSON_STAT(w, stats, req, rd_pt, "Requests");
	WJSON_STAT(w, stats, req, wr_pt, "Requests");
	WJSON_STAT(w, stats, req, serviced, "Requests");
	WJSON_STAT(w, stats, req, total, "Requests");
	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "blocks");
	WJSON_STAT(w, stats, blocks, core_volume_rd, "4KiB blocks");
	WJSON_STAT(w, stats, blocks, core_volume_wr, "4KiB blocks");
	WJSON_STAT(w, stats, blocks, core_volume_total, "4KiB blocks");
	WJSON_STAT(w, stats, blocks, cache_volume_rd, "4KiB blocks");
	WJSON_STAT(w, stats, blocks, cache_volume_wr, "4KiB blocks");
	WJSON_STAT(w, stats, blocks, cache_volume_total, "4KiB blocks");
	WJSON_STAT(w, stats, blocks, volume_rd, "4KiB blocks");
	WJSON_STAT(w, stats, blocks, volume_wr, "4KiB blocks");
	WJSON_STAT(w, stats, blocks, volume_total, "4KiB blocks");
	spdk_json_write_object_end(w);

	spdk_json_write_named_object_begin(w, "errors");
	WJSON_STAT(w, stats, errors, core_volume_rd, "Requests");
	WJSON_STAT(w, stats, errors, core_volume_wr, "Requests");
	WJSON_STAT(w, stats, errors, core_volume_total, "Requests");
	WJSON_STAT(w, stats, errors, cache_volume_rd, "Requests");
	WJSON_STAT(w, stats, errors, cache_volume_wr, "Requests");
	WJSON_STAT(w, stats, errors, cache_volume_total, "Requests");
	WJSON_STAT(w, stats, errors, total, "Requests");
	spdk_json_write_object_end(w);
}
