/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#ifndef SPDK_VBDEV_OCF_H
#define SPDK_VBDEV_OCF_H

#include "vbdev_ocf_cache.h"
#include "vbdev_ocf_core.h"

/* Callback for the RPC layer used in all management operations. */
typedef void (*vbdev_ocf_rpc_mngt_cb)(const char *bdev_name, void *cb_arg, int error);

/* Callback for the RPC layer used in bdev_ocf_get_bdevs call. */
typedef void (*vbdev_ocf_get_bdevs_cb)(void *cb_arg1, void *cb_arg2);

/*
 * RPC entry points:
 */

void vbdev_ocf_cache_start(const char *cache_name,
			   const char *base_name,
			   const char *cache_mode,
			   const uint8_t cache_line_size,
			   bool no_load,
			   vbdev_ocf_rpc_mngt_cb rpc_cb_fn,
			   void *rpc_cb_arg);

void vbdev_ocf_cache_stop(const char *cache_name,
			  vbdev_ocf_rpc_mngt_cb rpc_cb_fn,
			  void *rpc_cb_arg);

void vbdev_ocf_cache_attach(const char *cache_name,
			    const char *base_name,
			    bool force,
			    vbdev_ocf_rpc_mngt_cb rpc_cb_fn,
			    void *rpc_cb_arg);

void vbdev_ocf_cache_detach(const char *cache_name,
			    vbdev_ocf_rpc_mngt_cb rpc_cb_fn,
			    void *rpc_cb_arg);

void vbdev_ocf_core_add(const char *core_name,
			const char *base_name,
			const char *cache_name,
			vbdev_ocf_rpc_mngt_cb rpc_cb_fn,
			void *rpc_cb_arg);

void vbdev_ocf_core_remove(const char *core_name,
			   vbdev_ocf_rpc_mngt_cb rpc_cb_fn,
			   void *rpc_cb_arg);

void vbdev_ocf_set_cachemode(const char *cache_name,
			     const char *cache_mode,
			     vbdev_ocf_rpc_mngt_cb rpc_cb_fn,
			     void *rpc_cb_arg);

void vbdev_ocf_set_promotion(const char *cache_name, const char *policy,
			     int32_t nhit_insertion_threshold, int32_t nhit_trigger_threshold,
			     vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg);

void vbdev_ocf_set_cleaning(const char *cache_name, const char *policy, int32_t acp_wake_up_time,
			    int32_t acp_flush_max_buffers, int32_t alru_wake_up_time,
			    int32_t alru_flush_max_buffers, int32_t alru_staleness_time,
			    int32_t alru_activity_threshold, int32_t alru_max_dirty_ratio,
			    vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg);

void vbdev_ocf_set_seqcutoff(const char *bdev_name, const char *policy, int32_t threshold,
			     int32_t promotion_count, int32_t promote_on_threshold,
			     vbdev_ocf_rpc_mngt_cb rpc_cb_fn, void *rpc_cb_arg);

void vbdev_ocf_flush_start(const char *bdev_name,
			   vbdev_ocf_rpc_mngt_cb rpc_cb_fn,
			   void *rpc_cb_arg);

void vbdev_ocf_get_bdevs(const char *bdev_name,
			 vbdev_ocf_get_bdevs_cb rpc_cb_fn,
			 void *rpc_cb_arg1,
			 void *rpc_cb_arg2);

#endif
