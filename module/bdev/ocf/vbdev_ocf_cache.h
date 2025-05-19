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

struct vbdev_ocf_cache {
	struct ocf_mngt_cache_config		cache_cfg;
	struct ocf_mngt_cache_attach_config	cache_att_cfg;
	ocf_queue_t				cache_mngt_q;
	bool					no_load;

	struct vbdev_ocf_base			base;
};

struct vbdev_ocf_cache_mngt_queue_ctx {
	struct spdk_poller *		poller;
	struct spdk_thread *		thread;
	/* Currently kept only for its name used in debug log. */
	ocf_cache_t			cache; // rm ?
};

int vbdev_ocf_cache_create(ocf_cache_t *out, const char *cache_name, const char *cache_mode,
			   const uint8_t cache_line_size, bool no_load);
void vbdev_ocf_cache_destroy(ocf_cache_t cache);
int vbdev_ocf_cache_base_attach(ocf_cache_t cache, const char *base_name);
void vbdev_ocf_cache_base_detach(ocf_cache_t cache);
int vbdev_ocf_cache_config_volume_create(ocf_cache_t cache);
void vbdev_ocf_cache_config_volume_destroy(ocf_cache_t cache);
int vbdev_ocf_cache_mngt_queue_create(ocf_cache_t cache);

bool vbdev_ocf_cache_is_base_attached(ocf_cache_t cache);

#endif
