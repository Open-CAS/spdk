#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../..")
source "$rootdir/test/ocf/common.sh"

# change cache parameters and state during IO:

for cache_mode in "${cache_modes[@]}"; do
	start_bdevperf -c "$curdir/bdev_config.json"
	$bdevperf_py perform_tests -t 600 -q 128 -o 4096 -w randrw -M 50 &
	sleep 1

	$rpc_py bdev_ocf_set_cachemode Ocf_cache1 $cache_mode
	sleep 1

	nhit_insertion_threshold=$(random_number ${promotion_nhit_insertion_threshold_range[0]} \
		${promotion_nhit_insertion_threshold_range[1]})
	nhit_trigger_threshold=$(random_number ${promotion_nhit_trigger_threshold_range[0]} \
		${promotion_nhit_trigger_threshold_range[1]})
	$rpc_py bdev_ocf_set_promotion Ocf_cache1 \
		--policy nhit \
		--nhit-insertion-threshold $nhit_insertion_threshold \
		--nhit-trigger-threshold $nhit_trigger_threshold
	sleep 1
	$rpc_py bdev_ocf_set_promotion Ocf_cache1 --policy always
	sleep 1

	acp_wake_up_time=$(random_number ${cleaning_acp_wake_up_time_range[0]} \
		${cleaning_acp_wake_up_time_range[1]})
	acp_flush_max_buffers=$(random_number ${cleaning_acp_flush_max_buffers_range[0]} \
		${cleaning_acp_flush_max_buffers_range[1]})
	$rpc_py bdev_ocf_set_cleaning Ocf_cache1 \
		--policy acp \
		--acp-wake-up-time $acp_wake_up_time \
		--acp-flush-max-buffers $acp_flush_max_buffers
	sleep 1
	$rpc_py bdev_ocf_set_cleaning Ocf_cache1 --policy alru
	sleep 1

	seqcutoff_threshold=$(random_number ${seqcutoff_threshold_range[0]} \
		${seqcutoff_threshold_range[1]})
	seqcutoff_promotion_count=$(random_number ${seqcutoff_promotion_count_range[0]} \
		${seqcutoff_promotion_count_range[1]})
	seqcutoff_promote_on_threshold=$(random_number ${seqcutoff_promote_on_threshold_range[0]} \
		${seqcutoff_promote_on_threshold_range[1]})
	$rpc_py bdev_ocf_set_seqcutoff Ocf_cache1 \
		--policy always \
		--threshold $seqcutoff_threshold \
		--promotion-count $seqcutoff_promotion_count \
		--promote-on-threshold $seqcutoff_promote_on_threshold
	sleep 1
	$rpc_py bdev_ocf_set_seqcutoff Ocf_cache1 --policy never
	sleep 1

	$rpc_py bdev_ocf_detach_cache Ocf_cache1
	sleep 1
	$rpc_py bdev_ocf_attach_cache Ocf_cache1 Mal_cache1 -f
	sleep 1

	$rpc_py bdev_ocf_remove_core Ocf_core1-1
	sleep 1
	$rpc_py bdev_ocf_add_core Ocf_core1-1 Mal_core1-1 Ocf_cache1
	sleep 1

	$rpc_py bdev_ocf_get_stats Ocf_cache1
	sleep 1
	$rpc_py bdev_ocf_reset_stats Ocf_cache1
	sleep 1
	$rpc_py bdev_ocf_flush_start Ocf_cache1
	sleep 1
	$rpc_py bdev_ocf_get_bdevs
	sleep 1
	$rpc_py bdev_ocf_stop_cache Ocf_cache1
	sleep 1

	stop_bdevperf
done

# hot remove and examine during IO:

for cache_mode in "${cache_modes[@]}"; do
	start_bdevperf -c "$curdir/bdev_config.json"
	$bdevperf_py perform_tests -t 600 -q 128 -o 4096 -w randrw -M 50 &
	bpy_pid=$!
	sleep 1

	$rpc_py bdev_ocf_set_cachemode Ocf_cache1 $cache_mode
	sleep 1

	$rpc_py bdev_malloc_delete Mal_cache1
	sleep 1
	$rpc_py bdev_malloc_create -b Mal_cache1 200 512
	sleep 1

	$rpc_py bdev_malloc_delete Mal_core1-1
	sleep 1
	$rpc_py bdev_malloc_create -b Mal_core1-1 400 512
	sleep 1

	$rpc_py bdev_malloc_delete Mal_cache1
	sleep 1
	$rpc_py bdev_malloc_create -b Mal_cache1 200 512
	sleep 1

	$rpc_py bdev_malloc_delete Mal_core1-2
	sleep 1
	$rpc_py bdev_malloc_create -b Mal_core1-2 400 512
	sleep 1

	killprocess $bpy_pid
	sleep 1
	$bdevperf_py perform_tests -t 600 -q 128 -o 4096 -w randrw -M 50 &
	sleep 5

	stop_bdevperf
done
