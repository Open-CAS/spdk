/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#ifndef VBDEV_OCF_STATS_H
#define VBDEV_OCF_STATS_H

#include "spdk/json.h"
#include <ocf/ocf.h>

struct vbdev_ocf_stats {
	struct ocf_stats_usage usage;
	struct ocf_stats_requests req;
	struct ocf_stats_blocks blocks;
	struct ocf_stats_errors errors;
};

int vbdev_ocf_stats_cache_get(ocf_cache_t cache, struct vbdev_ocf_stats *stats);
int vbdev_ocf_stats_core_get(ocf_core_t core, struct vbdev_ocf_stats *stats);
int vbdev_ocf_stats_cache_reset(ocf_cache_t cache);
int vbdev_ocf_stats_core_reset(ocf_core_t core);

void vbdev_ocf_stats_write_json(struct spdk_json_write_ctx *w, struct vbdev_ocf_stats *stats);

#endif
