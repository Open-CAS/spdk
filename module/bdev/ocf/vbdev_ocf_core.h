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

/* Core wait list iterator. */
#define vbdev_ocf_foreach_core_in_waitlist(core_ctx) \
	STAILQ_FOREACH(core_ctx, &g_vbdev_ocf_core_waitlist, waitlist_entry)

/* Core context (assigned as core priv). */
struct vbdev_ocf_core {
	/* Exposed OCF vbdev; the one that is registered in bdev layer for usage. */
	struct spdk_bdev		ocf_vbdev;

	/* Base bdev data. */
	struct vbdev_ocf_base		base;

	/* Core add config. */
	struct ocf_mngt_core_config	core_cfg;

	/* Name of cache that this core is assigned to. */
	char				cache_name[OCF_CACHE_NAME_SIZE];

	/* Context for cache stop management operation. */
	struct vbdev_ocf_mngt_ctx *	mngt_ctx;

	/* Status of core flush operation. */
	struct {
		bool in_progress;
		int error;
	} flush;

	STAILQ_ENTRY(vbdev_ocf_core)	waitlist_entry;
};

/* Core IO channel context. */
struct vbdev_ocf_core_io_channel_ctx {
	/* OCF queue. */
	ocf_queue_t			queue;

	/* Cache base bdev IO channel. */
	struct spdk_io_channel *	cache_ch;

	/* Core base bdev IO channel. */
	struct spdk_io_channel *	core_ch;

	/* Registered poller. */
	struct spdk_poller *		poller;

	/* Thread on which poller was registered and channels were opened. */
	struct spdk_thread *		thread;

	/* Currently kept only for its name used in debug log. */
	ocf_core_t			core;
};

/*
 * Helpers:
 */

/* Add core context to wait list. */
void vbdev_ocf_core_waitlist_add(struct vbdev_ocf_core *core_ctx);

/* Remove core context from wait list. */
void vbdev_ocf_core_waitlist_remove(struct vbdev_ocf_core *core_ctx);

/* Get core from wait list by its name. */
struct vbdev_ocf_core *vbdev_ocf_core_waitlist_get_by_name(const char *core_name);

/* Return core name string from given core context. */
char *vbdev_ocf_core_get_name(struct vbdev_ocf_core *core_ctx);

/* Check if core base is already attached. */
bool vbdev_ocf_core_is_base_attached(struct vbdev_ocf_core *core_ctx);

/* Check if core was added to cache from metadata during cache load. */
bool vbdev_ocf_core_is_loaded(const char *core_name);

/*
 * Management:
 */

/* Create core context and fill config. */
int vbdev_ocf_core_create(struct vbdev_ocf_core **out, const char *core_name, const char *cache_name);

/* Free core context. */
void vbdev_ocf_core_destroy(struct vbdev_ocf_core *core_ctx);

/* Open, claim and create IO channel for base bdev. */
int vbdev_ocf_core_base_attach(struct vbdev_ocf_core *core_ctx, const char *base_name);

/* Close base bdev and IO channel. */
void vbdev_ocf_core_base_detach(struct vbdev_ocf_core *core_ctx);

/* Register core in SPDK bdev layer for usage. */
int vbdev_ocf_core_register(ocf_core_t core);

/* Unregister core from SPDK bdev layer. */
int vbdev_ocf_core_unregister(struct vbdev_ocf_core *core_ctx, spdk_bdev_unregister_cb cb_fn, void *cb_arg);

/* Add all cores from wait list assigned to given cache. */
void vbdev_ocf_core_add_from_waitlist(ocf_cache_t cache);

/* Create new cache IO channel on all opened channels context for each opened core. */
int vbdev_ocf_core_create_cache_channel(ocf_cache_t cache);

/* Destroy cache IO channel on all opened channels context for each opened core. */
int vbdev_ocf_core_destroy_cache_channel(ocf_cache_t cache);

#endif
