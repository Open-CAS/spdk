/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#ifndef SPDK_VBDEV_OCF_H
#define SPDK_VBDEV_OCF_H

#include "vbdev_ocf_cache.h"
#include "vbdev_ocf_core.h"

typedef void (*vbdev_ocf_rpc_mngt_cb)(const char *bdev_name, void *cb_arg, int error);
typedef void (*vbdev_ocf_get_bdevs_cb)(void *cb_arg1, void *cb_arg2);

struct vbdev_ocf_mngt_ctx {
	ocf_cache_t			cache;
	ocf_core_t			core;
	struct vbdev_ocf_core *		core_ctx;
	ocf_mngt_cache_attach_end_t	attach_cb_fn;
	vbdev_ocf_rpc_mngt_cb		rpc_cb_fn;
	void *				rpc_cb_arg;
};

/* RPC entry points. */

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
			   const char *cache_name,
			   vbdev_ocf_rpc_mngt_cb rpc_cb_fn,
			   void *rpc_cb_arg);

void vbdev_ocf_get_bdevs(const char *name,
			 vbdev_ocf_get_bdevs_cb rpc_cb_fn,
			 void *rpc_cb_arg1,
			 void *rpc_cb_arg2);

#endif
