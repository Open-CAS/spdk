/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#ifndef SPDK_VBDEV_OCF_CACHE_H
#define SPDK_VBDEV_OCF_CACHE_H

#include <ocf/ocf.h>
#include "spdk/bdev_module.h"
#include "volume.h"

/* OCF module interface. */
extern struct spdk_bdev_module ocf_if;

/* Cache context (assigned as cache priv). */
struct vbdev_ocf_cache {
	/* Base bdev data. */
	struct vbdev_ocf_base			base;
	/* Cache start config. */
	struct ocf_mngt_cache_config		cache_cfg;
	/* Cache device attach config. */
	struct ocf_mngt_cache_attach_config	cache_att_cfg;
	/* Cache management queue. */
	ocf_queue_t				cache_mngt_q;
	/* Indicator for cache start to ignore cache metadata found on device. */
	bool					no_load;
	/* Status of cache flush operation. */
	struct {
		bool in_progress;
		int error;
	} flush;
};

typedef void (*vbdev_ocf_rpc_mngt_cb)(const char *bdev_name, void *cb_arg, int error);

/* Temporary context for management operations. */
struct vbdev_ocf_mngt_ctx {
	vbdev_ocf_rpc_mngt_cb		rpc_cb_fn;
	void *				rpc_cb_arg;
	const char *			bdev_name;
	ocf_cache_t			cache;
	ocf_core_t			core;
	union {
		ocf_mngt_cache_attach_end_t	att_cb_fn;
		struct vbdev_ocf_core *		core_ctx;
		ocf_cache_mode_t		cache_mode;
		struct {
			ocf_promotion_t		policy;
			int32_t			nhit_insertion_threshold;
			int32_t			nhit_trigger_threshold;
		} promotion;
		struct {
			ocf_cleaning_t		policy;
			int32_t			acp_wake_up_time;
			int32_t			acp_flush_max_buffers;
			int32_t			alru_wake_up_time;
			int32_t			alru_flush_max_buffers;
			int32_t			alru_staleness_time;
			int32_t			alru_activity_threshold;
			int32_t			alru_max_dirty_ratio;
		} cleaning;
		struct {
			ocf_seq_cutoff_policy	policy;
			int32_t			threshold;
			int32_t			promotion_count;
			int32_t			promote_on_threshold;
		} seqcutoff;
	} u;
};

/* Cache management queue context. */
struct vbdev_ocf_cache_mngt_queue_ctx {
	/* Registered poller. */
	struct spdk_poller *		poller;
	/* Thread on which poller was registered. */
	struct spdk_thread *		thread;
	/* Currently kept only for its name used in debug log. */
	ocf_cache_t			cache; // rm ?
};

/* Check if cache base is already attached. */
bool vbdev_ocf_cache_is_base_attached(ocf_cache_t cache);

/* Create cache context, fill config and start OCF cache. */
int vbdev_ocf_cache_create(ocf_cache_t *out, const char *cache_name, const char *cache_mode,
			   const uint8_t cache_line_size, bool no_load);
/* Free cache context. */
void vbdev_ocf_cache_destroy(ocf_cache_t cache);
/* Open, claim and create IO channel for base bdev. */
int vbdev_ocf_cache_base_attach(ocf_cache_t cache, const char *base_name);
/* Close base bdev and IO channel. */
void vbdev_ocf_cache_base_detach(ocf_cache_t cache);
/* Create OCF cache volume and fill device config. */
int vbdev_ocf_cache_config_volume_create(ocf_cache_t cache);
/* Destroy OCF cache volume from config. */
void vbdev_ocf_cache_config_volume_destroy(ocf_cache_t cache);
/* Attach or load cache device. */
int vbdev_ocf_cache_volume_attach(ocf_cache_t cache, struct vbdev_ocf_mngt_ctx *ctx);
/* Create cache management queue poller. */
int vbdev_ocf_cache_mngt_queue_create(ocf_cache_t cache);

#endif
