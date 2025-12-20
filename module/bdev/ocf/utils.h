/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#ifndef VBDEV_OCF_UTILS_H
#define VBDEV_OCF_UTILS_H

#include <ocf/ocf.h>

/* Get OCF cache mode by its name. */
ocf_cache_mode_t vbdev_ocf_cachemode_get_by_name(const char *cache_mode_name);

/* Get the name of OCF cache mode. */
const char *vbdev_ocf_cachemode_get_name(ocf_cache_mode_t cache_mode);

/* Get OCF promotion policy by its name. */
ocf_promotion_t vbdev_ocf_promotion_policy_get_by_name(const char *policy_name);

/* Get the name of OCF promotion policy. */
const char *vbdev_ocf_promotion_policy_get_name(ocf_promotion_t policy);

/* Get OCF cleaning policy by its name. */
ocf_cleaning_t vbdev_ocf_cleaning_policy_get_by_name(const char *policy_name);

/* Get the name of OCF cleaning policy. */
const char *vbdev_ocf_cleaning_policy_get_name(ocf_cleaning_t policy);

/* Get OCF sequential cut-off policy by its name. */
ocf_seq_cutoff_policy vbdev_ocf_seqcutoff_policy_get_by_name(const char *policy_name);

/* Get the name of OCF sequential cut-off policy. */
const char *vbdev_ocf_seqcutoff_policy_get_name(ocf_seq_cutoff_policy policy);

#endif
