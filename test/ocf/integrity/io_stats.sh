#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../..")
source "$rootdir/test/ocf/common.sh"

io_patterns=("read" "write" "rw" "randread" "randwrite" "randrw" "unmap" "flush")
io_depth=(1 16 128)
io_size=(512 4096)
rw_mix=50
runtime=10
cpu_mask=0xF

start_bdevperf -c "$curdir/bdev_config.json" -m $cpu_mask -C
for cache_mode in "${cache_modes[@]}"; do
	$rpc_py bdev_ocf_set_cachemode Ocf_cache1 $cache_mode
	for id in "${io_depth[@]}"; do
		for is in "${io_size[@]}"; do
			for ip in "${io_patterns[@]}"; do
				$rpc_py bdev_ocf_reset_stats Ocf_cache1
				test_results=$($bdevperf_py perform_tests -t $runtime -q $id \
					-o $is -w $ip $([[ $ip =~ rw$ ]] && echo "-M $rw_mix"))
				stats_core1=$($rpc_py bdev_ocf_get_stats Ocf_core1-1)
				stats_core2=$($rpc_py bdev_ocf_get_stats Ocf_core1-2)

				xtrace_disable
				reqs_sent_core1=$(echo $test_results | jq -e \
					'[.results[] | select(.job == "Ocf_core1-1") |
					.runtime * .iops] | add | round')
				reqs_sent_core2=$(echo $test_results | jq -e \
					'[.results[] | select(.job == "Ocf_core1-2") |
					.runtime * .iops] | add | round')
				reqs_recv_rd_core1=$(echo $stats_core1 | jq -e \
					'.requests | .rd_total.count + .rd_pt.count')
				reqs_recv_rd_core2=$(echo $stats_core2 | jq -e \
					'.requests | .rd_total.count + .rd_pt.count')
				reqs_recv_wr_core1=$(echo $stats_core1 | jq -e \
					'.requests | .wr_total.count + .wr_pt.count')
				reqs_recv_wr_core2=$(echo $stats_core2 | jq -e \
					'.requests | .wr_total.count + .wr_pt.count')
				reqs_recv_total_core1=$(echo $stats_core1 | jq -e '.requests.total.count')
				reqs_recv_total_core2=$(echo $stats_core2 | jq -e '.requests.total.count')
				xtrace_restore

				if [[ $ip == "unmap" || $ip == "flush" ]]; then
					continue
				elif [[ $ip =~ read$ ]]; then
					[ $reqs_recv_rd_core1 -eq $reqs_sent_core1 ]
					[ $reqs_recv_rd_core2 -eq $reqs_sent_core2 ]
					[ $reqs_recv_wr_core1 -eq 0 ]
					[ $reqs_recv_wr_core2 -eq 0 ]
				elif [[ $ip =~ write$ ]]; then
					[ $reqs_recv_rd_core1 -eq 0 ]
					[ $reqs_recv_rd_core2 -eq 0 ]
					[ $reqs_recv_wr_core1 -eq $reqs_sent_core1 ]
					[ $reqs_recv_wr_core2 -eq $reqs_sent_core2 ]
				elif [[ $ip =~ rw$ ]]; then
					[ $reqs_recv_rd_core1 -gt 0 ]
					[ $reqs_recv_rd_core2 -gt 0 ]
					[ $reqs_recv_wr_core1 -gt 0 ]
					[ $reqs_recv_wr_core2 -gt 0 ]
					reqs_rw_recv_core1=$((reqs_recv_rd_core1 + reqs_recv_wr_core1))
					reqs_rw_recv_core2=$((reqs_recv_rd_core2 + reqs_recv_wr_core2))
					[ $reqs_rw_recv_core1 -eq $reqs_sent_core1 ]
					[ $reqs_rw_recv_core2 -eq $reqs_sent_core2 ]
				fi

				[ $reqs_recv_total_core1 -eq $reqs_sent_core1 ]
				[ $reqs_recv_total_core2 -eq $reqs_sent_core2 ]
			done
		done
	done
done
stop_bdevperf
