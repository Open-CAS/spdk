#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../..")
source "$rootdir/test/ocf/common.sh"

# cache line sizes and cache modes:

for cache_line_size in "${cache_line_sizes[@]}"; do
	for add_cores in false true; do
		for create_caches in false true; do
			for stop_caches in false true; do
				start_spdk
				start_caches_with_cache_line_size $cache_line_size
				if [ $create_caches == true ]; then
					create_caches
					__check_caches_attached
					if [ $add_cores == true ]; then
						create_cores
						add_cores
						__check_cores_attached
					fi
				else
					__check_caches_detached
				fi
				__check_cache_line_size $cache_line_size
				for cache_mode in "${cache_modes[@]}"; do
					set_cache_mode $cache_mode
					__check_cache_mode $cache_mode
				done
				if [ $stop_caches == true ]; then
					stop_caches
					__check_caches_empty
				fi
				stop_spdk
			done
		done
	done
done

# promotion params:

for promotion_policy in "${promotion_policies[@]}"; do
	for add_cores in false true; do
		for stop_caches in false true; do
			start_spdk
			create_caches
			start_caches
			__check_caches_attached
			if [ $add_cores == true ]; then
				create_cores
				add_cores
				__check_cores_attached
			fi
			if [ $promotion_policy == always ]; then
				set_promotion_always_params
				__check_promotion_always_params
			elif [ $promotion_policy == nhit ]; then
				insertion_threshold=$(random_number ${promotion_nhit_insertion_threshold_range[0]} ${promotion_nhit_insertion_threshold_range[1]})
				trigger_threshold=$(random_number ${promotion_nhit_trigger_threshold_range[0]} ${promotion_nhit_trigger_threshold_range[1]})
				set_promotion_nhit_params $insertion_threshold $trigger_threshold
				__check_promotion_nhit_params $insertion_threshold $trigger_threshold
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done

# cleaning params:

for cleaning_policy in "${cleaning_policies[@]}"; do
	for add_cores in false true; do
		for stop_caches in false true; do
			start_spdk
			create_caches
			start_caches
			__check_caches_attached
			if [ $add_cores == true ]; then
				create_cores
				add_cores
				__check_cores_attached
			fi
			if [ $cleaning_policy == alru ]; then
				wake_up_time=$(random_number ${cleaning_alru_wake_up_time_range[0]} ${cleaning_alru_wake_up_time_range[1]})
				flush_max_buffers=$(random_number ${cleaning_alru_flush_max_buffers_range[0]} ${cleaning_alru_flush_max_buffers_range[1]})
				staleness_time=$(random_number ${cleaning_alru_staleness_time_range[0]} ${cleaning_alru_staleness_time_range[1]})
				activity_threshold=$(random_number ${cleaning_alru_activity_threshold_range[0]} ${cleaning_alru_activity_threshold_range[1]})
				max_dirty_ratio=$(random_number ${cleaning_alru_max_dirty_ratio_range[0]} ${cleaning_alru_max_dirty_ratio_range[1]})
				set_cleaning_alru_params $wake_up_time $flush_max_buffers $staleness_time $activity_threshold $max_dirty_ratio
				__check_cleaning_alru_params $wake_up_time $flush_max_buffers $staleness_time $activity_threshold $max_dirty_ratio
			elif [ $cleaning_policy == acp ]; then
				wake_up_time=$(random_number ${cleaning_acp_wake_up_time_range[0]} ${cleaning_acp_wake_up_time_range[1]})
				flush_max_buffers=$(random_number ${cleaning_acp_flush_max_buffers_range[0]} ${cleaning_acp_flush_max_buffers_range[1]})
				set_cleaning_acp_params $wake_up_time $flush_max_buffers
				__check_cleaning_acp_params $wake_up_time $flush_max_buffers
			elif [ $cleaning_policy == nop ]; then
				set_cleaning_nop_params
				__check_cleaning_nop_params
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done

# sequential cut-off params:

for device in core cache; do
	for seqcutoff_policy in "${seqcutoff_policies[@]}"; do
		for stop_caches in false true; do
			start_spdk
			create_caches
			create_cores
			start_caches
			add_cores
			__check_caches_attached
			__check_cores_attached
			threshold=$(random_number ${seqcutoff_threshold_range[0]} ${seqcutoff_threshold_range[1]})
			promotion_count=$(random_number ${seqcutoff_promotion_count_range[0]} ${seqcutoff_promotion_count_range[1]})
			promote_on_threshold=$(random_number ${seqcutoff_promote_on_threshold_range[0]} ${seqcutoff_promote_on_threshold_range[1]})
			if [ $device == core ]; then
				set_seqcutoff_params $seqcutoff_policy $threshold $promotion_count $promote_on_threshold
				__check_seqcutoff_params $seqcutoff_policy $threshold $promotion_count $promote_on_threshold
			elif [ $device == cache ]; then
				set_seqcutoff_params_all $seqcutoff_policy $threshold $promotion_count $promote_on_threshold
				__check_seqcutoff_params_all $seqcutoff_policy $threshold $promotion_count $promote_on_threshold
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done
