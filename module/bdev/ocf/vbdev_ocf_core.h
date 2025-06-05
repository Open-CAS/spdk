/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#ifndef SPDK_VBDEV_OCF_CORE_H
#define SPDK_VBDEV_OCF_CORE_H

#include <ocf/ocf.h>
#include "spdk/bdev_module.h"
#include "volume.h"

/* OCF module interface. */
extern struct spdk_bdev_module ocf_if;

/* Function table of exposed OCF vbdev. */
extern struct spdk_bdev_fn_table vbdev_ocf_fn_table;

/* Global wait list of all incomplete cores (not added to
 * the cache yet due to lack of cache or base device. */
extern STAILQ_HEAD(vbdev_ocf_core_waitlist_head, vbdev_ocf_core) g_vbdev_ocf_core_waitlist;

#define vbdev_ocf_foreach_core_in_waitlist(core_ctx) \
	STAILQ_FOREACH(core_ctx, &g_vbdev_ocf_core_waitlist, waitlist_entry)

struct vbdev_ocf_core {
	struct ocf_mngt_core_config		core_cfg;

	/* Exposed OCF vbdev; the one which is registered in bdev layer for usage. */
	struct spdk_bdev			ocf_vbdev;

	struct vbdev_ocf_base			base;
	char					cache_name[OCF_CACHE_NAME_SIZE];
	struct vbdev_ocf_mngt_ctx *		mngt_ctx;

	STAILQ_ENTRY(vbdev_ocf_core)		waitlist_entry;
};

struct vbdev_ocf_core_io_channel_ctx {
	ocf_queue_t			queue;
	struct spdk_io_channel *	cache_ch;
	struct spdk_io_channel *	core_ch;
	struct spdk_poller *		poller;
	struct spdk_thread *		thread;
	/* Currently kept only for its name used in debug log. */
	ocf_core_t			core; // rm ?
};

int vbdev_ocf_core_create(struct vbdev_ocf_core **out, const char *core_name, const char *cache_name);
void vbdev_ocf_core_destroy(struct vbdev_ocf_core *core_ctx);
int vbdev_ocf_core_base_attach(struct vbdev_ocf_core *core_ctx, const char *base_name);
void vbdev_ocf_core_base_detach(struct vbdev_ocf_core *core_ctx);
int vbdev_ocf_core_register(ocf_core_t core);
int vbdev_ocf_core_unregister(struct vbdev_ocf_core *core_ctx, spdk_bdev_unregister_cb cb_fn, void *cb_arg);

char *vbdev_ocf_core_get_name(struct vbdev_ocf_core *core_ctx);
struct vbdev_ocf_core *vbdev_ocf_core_waitlist_get_by_name(const char *core_name);
bool vbdev_ocf_core_is_base_attached(struct vbdev_ocf_core *core_ctx);
bool vbdev_ocf_core_is_loaded(const char *core_name);

#endif
