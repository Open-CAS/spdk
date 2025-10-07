#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../..")
source "$rootdir/test/ocf/common.sh"

devices=("Ocf_cache1" "Ocf_core1-1" "Ocf_core1-2")
cache_modes=("wb" "wo")
io_patterns=("write" "rw" "randwrite" "randrw")
io_depth=128
io_size=4096
rw_mix=50
runtime=10

check_flush_in_progress() {
	$rpc_py bdev_ocf_get_bdevs $1 | jq -e '.flush.in_progress' > /dev/null
}

start_bdevperf -c "$curdir/bdev_config.json"
$rpc_py bdev_ocf_set_seqcutoff Ocf_cache1 --policy never
$rpc_py bdev_ocf_set_promotion Ocf_cache1 --policy always
$rpc_py bdev_ocf_set_cleaning Ocf_cache1 --policy nop

for cache_mode in "${cache_modes[@]}"; do
	$rpc_py bdev_ocf_set_cachemode Ocf_cache1 $cache_mode
	for ip in "${io_patterns[@]}"; do
		for dev in "${devices[@]}"; do
			$bdevperf_py perform_tests -t $runtime -q $io_depth -o $io_size \
				-w $ip $([[ $ip =~ rw$ ]] && echo "-M $rw_mix")
			for dv in "${devices[@]}"; do
				$rpc_py bdev_ocf_get_stats $dv | jq -e '.usage | .dirty.count > .clean.count'
			done

			$rpc_py bdev_ocf_flush_start $dev
			while check_flush_in_progress $dev; do
				sleep 1
			done

			$rpc_py bdev_ocf_get_bdevs $dev | jq -e '.flush.error == 0'
			$rpc_py bdev_ocf_get_stats $dev | jq -e '.usage | .dirty.count == 0'
		done
	done
done
stop_bdevperf
