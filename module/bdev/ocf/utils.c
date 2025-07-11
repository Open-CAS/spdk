/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   Copyright (C) 2025 Huawei Technologies
 *   All rights reserved.
 */

#include "utils.h"

static char *cache_modes[ocf_cache_mode_max] = {
	[ocf_cache_mode_wt] = "wt",
	[ocf_cache_mode_wb] = "wb",
	[ocf_cache_mode_wa] = "wa",
	[ocf_cache_mode_pt] = "pt",
	[ocf_cache_mode_wi] = "wi",
	[ocf_cache_mode_wo] = "wo",
};

static char *promotion_policies[ocf_promotion_max] = {
	[ocf_promotion_always] = "always",
	[ocf_promotion_nhit] = "nhit",
};

static char *cleaning_policies[ocf_cleaning_max] = {
	[ocf_cleaning_nop] = "nop",
	[ocf_cleaning_alru] = "alru",
	[ocf_cleaning_acp] = "acp",
};

static char *seqcutoff_policies[ocf_seq_cutoff_policy_max] = {
	[ocf_seq_cutoff_policy_always] = "always",
	[ocf_seq_cutoff_policy_full] = "full",
	[ocf_seq_cutoff_policy_never] = "never",
};

ocf_cache_mode_t
vbdev_ocf_cachemode_get_by_name(const char *cache_mode_name)
{
	ocf_cache_mode_t cache_mode;

	if (!cache_mode_name) {
		return ocf_cache_mode_none;
	}

	for (cache_mode = 0; cache_mode < ocf_cache_mode_max; cache_mode++) {
		if (!strcmp(cache_mode_name, cache_modes[cache_mode])) {
			return cache_mode;
		}
	}

	return ocf_cache_mode_none;
}

const char *
vbdev_ocf_cachemode_get_name(ocf_cache_mode_t cache_mode)
{
	if (cache_mode > ocf_cache_mode_none && cache_mode < ocf_cache_mode_max) {
		return cache_modes[cache_mode];
	}

	return NULL;
}

ocf_promotion_t
vbdev_ocf_promotion_policy_get_by_name(const char *policy_name)
{
	ocf_promotion_t policy;

	if (!policy_name) {
		return -EINVAL;
	}

	for (policy = 0; policy < ocf_promotion_max; policy++)
		if (!strcmp(policy_name, promotion_policies[policy])) {
			return policy;
		}

	return -EINVAL;
}

const char *
vbdev_ocf_promotion_policy_get_name(ocf_promotion_t policy)
{
	if (policy >= ocf_promotion_always && policy < ocf_promotion_max) {
		return promotion_policies[policy];
	}

	return NULL;
}

ocf_cleaning_t
vbdev_ocf_cleaning_policy_get_by_name(const char *policy_name)
{
	ocf_cleaning_t policy;

	if (!policy_name) {
		return -EINVAL;
	}

	for (policy = 0; policy < ocf_cleaning_max; policy++)
		if (!strcmp(policy_name, cleaning_policies[policy])) {
			return policy;
		}

	return -EINVAL;
}

const char *
vbdev_ocf_cleaning_policy_get_name(ocf_cleaning_t policy)
{
	if (policy >= ocf_cleaning_nop && policy < ocf_cleaning_max) {
		return cleaning_policies[policy];
	}

	return NULL;
}

ocf_seq_cutoff_policy
vbdev_ocf_seqcutoff_policy_get_by_name(const char *policy_name)
{
	ocf_seq_cutoff_policy policy;

	if (!policy_name) {
		return -EINVAL;
	}

	for (policy = 0; policy < ocf_seq_cutoff_policy_max; policy++)
		if (!strcmp(policy_name, seqcutoff_policies[policy])) {
			return policy;
		}

	return -EINVAL;
}

const char *
vbdev_ocf_seqcutoff_policy_get_name(ocf_seq_cutoff_policy policy)
{
	if (policy >= ocf_seq_cutoff_policy_always && policy < ocf_seq_cutoff_policy_max) {
		return seqcutoff_policies[policy];
	}

	return NULL;
}
