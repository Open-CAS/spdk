#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../..")
source "$rootdir/test/ocf/common.sh"

app_pid=
bg_pids=()

start_spdk_() {
	"$SPDK_BIN_DIR/spdk_tgt" -L vbdev_ocf "$@" &
	app_pid=$!
	waitforlisten $app_pid
}

stop_spdk_() {
	killprocess $app_pid
}

start_bdevperf_() {
	"$SPDK_EXAMPLE_DIR/bdevperf" -z -L vbdev_ocf -c "$curdir/bdev_config.json" &
	app_pid=$!
	waitforlisten $app_pid
	$bdevperf_py perform_tests -t 600 -q 128 -o 4096 -w randrw -M 50 &
}

stop_bdevperf_() {
	killprocess $app_pid
}

run_in_bg() {
	xtrace_disable
	(while true; do
		"$@" &> /dev/null || true
		sleep 0.1
	done) &
	bg_pids+=($!)
	xtrace_restore
}

cleanup() {
	for pid in "${bg_pids[@]}"; do
		killprocess $pid || true
	done
	killprocess $app_pid
}

trap 'cleanup; exit 1' SIGINT SIGTERM EXIT

run_in_bg $rpc_py bdev_ocf_get_bdevs
run_in_bg $rpc_py bdev_ocf_get_bdevs Ocf_cache1
run_in_bg $rpc_py bdev_ocf_get_bdevs Ocf_core1-1
run_in_bg $rpc_py bdev_ocf_get_bdevs Ocf_core1-2
run_in_bg $rpc_py bdev_ocf_get_bdevs Ocf_core1-3
run_in_bg $rpc_py bdev_ocf_get_bdevs Ocf_cache3
run_in_bg $rpc_py bdev_ocf_get_bdevs Ocf_core3-1
run_in_bg $rpc_py bdev_ocf_get_bdevs Ocf_core3-2
run_in_bg $rpc_py bdev_ocf_get_bdevs Ocf_core3-3
run_in_bg $rpc_py bdev_ocf_get_stats Ocf_cache1
run_in_bg $rpc_py bdev_ocf_get_stats Ocf_core1-1
run_in_bg $rpc_py bdev_ocf_get_stats Ocf_core1-2
run_in_bg $rpc_py bdev_ocf_get_stats Ocf_core1-3
run_in_bg $rpc_py bdev_ocf_get_stats Ocf_cache3
run_in_bg $rpc_py bdev_ocf_get_stats Ocf_core3-1
run_in_bg $rpc_py bdev_ocf_get_stats Ocf_core3-2
run_in_bg $rpc_py bdev_ocf_get_stats Ocf_core3-3

# Test fast setup under the load of multiple info dumping RPC calls.

# basic:

start_spdk_
create_caches_do
create_cores_do
start_caches_do
add_cores_do
__check_setup_completed
stop_spdk_

start_spdk_
create_caches_do
create_cores_do
add_cores_do
start_caches_do
__check_setup_completed
stop_spdk_

start_spdk_
create_cores_do
create_caches_do
start_caches_do
add_cores_do
__check_setup_completed
stop_spdk_

start_spdk_
create_cores_do
create_caches_do
add_cores_do
start_caches_do
__check_setup_completed
stop_spdk_

# examine:

start_spdk_
start_caches_do
add_cores_do
create_caches_do
create_cores_do
__check_setup_completed
stop_spdk_

start_spdk_
start_caches_do
add_cores_do
create_cores_do
create_caches_do
__check_setup_completed
stop_spdk_

start_spdk_
add_cores_do
start_caches_do
create_caches_do
create_cores_do
__check_setup_completed
stop_spdk_

start_spdk_
add_cores_do
start_caches_do
create_cores_do
create_caches_do
__check_setup_completed
stop_spdk_

# load:

start_spdk_
create_caches_persistent_with_metadata_with_cores
destroy_caches_persistent
start_caches_do
add_cores_do
create_caches_persistent_do
create_cores_do
__check_setup_completed
stop_spdk_

start_spdk_
create_caches_persistent_with_metadata_with_cores
destroy_caches_persistent
start_caches_do
add_cores_do
create_cores_do
create_caches_persistent_do
__check_setup_completed
stop_spdk_

start_spdk_
create_caches_persistent_with_metadata_with_cores
destroy_caches_persistent
add_cores_do
start_caches_do
create_caches_persistent_do
create_cores_do
__check_setup_completed
stop_spdk_

start_spdk_
create_caches_persistent_with_metadata_with_cores
destroy_caches_persistent
add_cores_do
start_caches_do
create_cores_do
create_caches_persistent_do
__check_setup_completed
stop_spdk_

# detach-attach:

start_spdk_
create_caches_do
create_cores_do
start_caches_do
add_cores_do
detach_caches_do
attach_caches_do
__check_setup_completed
stop_spdk_

start_spdk_
create_caches_do
create_cores_do
start_caches_do
detach_caches_do
add_cores_do
attach_caches_do
__check_setup_completed
stop_spdk_

start_spdk_
create_caches_do
create_cores_do
start_caches_do
add_cores_do
detach_caches_do
remove_cores_do
attach_caches_do
add_cores_do
__check_setup_completed
stop_spdk_

# hotremove:

start_spdk_
create_caches_do
start_caches_do
destroy_caches_do
stop_spdk_

start_spdk_
create_cores_do
add_cores_do
destroy_cores_do
stop_spdk_

start_spdk_
create_caches_do
create_cores_do
start_caches_do
add_cores_do
destroy_caches_do
create_caches_do
__check_setup_completed
stop_spdk_

start_spdk_
create_caches_do
create_cores_do
start_caches_do
add_cores_do
destroy_cores_do
create_cores_do
__check_setup_completed
stop_spdk_

start_spdk_
create_caches_do
create_cores_do
start_caches_do
add_cores_do
destroy_caches_do
destroy_cores_do
create_caches_do
create_cores_do
__check_setup_completed
stop_spdk_

# management during IO:

start_bdevperf_
for cache_mode in "${cache_modes[@]}"; do
	$rpc_py bdev_ocf_set_cachemode Ocf_cache1 $cache_mode
	$rpc_py bdev_ocf_set_promotion Ocf_cache1 \
		--policy nhit \
		--nhit-insertion-threshold ${promotion_nhit_insertion_threshold_range[1]} \
		--nhit-trigger-threshold ${promotion_nhit_trigger_threshold_range[1]}
	$rpc_py bdev_ocf_set_promotion Ocf_cache1 --policy always
	$rpc_py bdev_ocf_set_cleaning Ocf_cache1 \
		--policy acp \
		--acp-wake-up-time ${cleaning_acp_wake_up_time_range[1]} \
		--acp-flush-max-buffers ${cleaning_acp_flush_max_buffers_range[1]}
	$rpc_py bdev_ocf_set_cleaning Ocf_cache1 --policy alru
	$rpc_py bdev_ocf_set_seqcutoff Ocf_cache1 \
		--policy always \
		--threshold ${seqcutoff_threshold_range[1]} \
		--promotion-count ${seqcutoff_promotion_count_range[1]} \
		--promote-on-threshold ${seqcutoff_promote_on_threshold_range[1]}
	$rpc_py bdev_ocf_set_seqcutoff Ocf_cache1 --policy never
	$rpc_py bdev_ocf_detach_cache Ocf_cache1
	$rpc_py bdev_ocf_attach_cache Ocf_cache1 Mal_cache1 -f
	$rpc_py bdev_ocf_remove_core Ocf_core1-1
	$rpc_py bdev_ocf_add_core Ocf_core1-1 Mal_core1-1 Ocf_cache1
	$rpc_py bdev_ocf_reset_stats Ocf_cache1
	$rpc_py bdev_ocf_flush_start Ocf_cache1
	$rpc_py bdev_malloc_delete Mal_cache1
	$rpc_py bdev_malloc_create -b Mal_cache1 200 512
	$rpc_py bdev_malloc_delete Mal_core1-1
	$rpc_py bdev_malloc_create -b Mal_core1-1 400 512
done
stop_bdevperf_

trap - SIGINT SIGTERM EXIT
cleanup
