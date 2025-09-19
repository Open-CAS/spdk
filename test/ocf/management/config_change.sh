#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source "$rootdir/test/ocf/common.sh"

cache_modes=("wt" "wb" "wa" "wo" "wi" "pt")

promotion_policies=("always" "nhit")
promotion_nhit_insertion_threshold_range=(2 1000)
promotion_nhit_trigger_threshold_range=(0 100)

cleaning_policies=("alru" "acp" "nop")
cleaning_acp_wake_up_time_range=(0 10000)
cleaning_acp_flush_max_buffers_range=(1 10000)
cleaning_alru_wake_up_time_range=(0 3600)
cleaning_alru_flush_max_buffers_range=(1 10000)
cleaning_alru_staleness_time_range=(1 3600)
cleaning_alru_activity_threshold_range=(0 1000000)
cleaning_alru_max_dirty_ratio_range=(0 100)

seqcutoff_policies=("always" "full" "never")
seqcutoff_policy_default="full"
seqcutoff_threshold_range=(1 4194181)
seqcutoff_threshold_default=1
seqcutoff_promotion_count_range=(1 65535)
seqcutoff_promotion_count_default=8
seqcutoff_promote_on_threshold_range=(0 1)
seqcutoff_promote_on_threshold_default=false

set_cache_mode() {
	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local cache_mode=$1

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_cachemode Ocf_cache$i $cache_mode
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_promotion_always_params() {
	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_promotion Ocf_cache$i \
			--policy $policy
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_promotion_nhit_params() {
	if [ $# -ne 3 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local insertion_threshold=$2
	local trigger_threshold=$3

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_promotion Ocf_cache$i \
			--policy $policy \
			--nhit-insertion-threshold $insertion_threshold \
			--nhit-trigger-threshold $trigger_threshold
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_cleaning_alru_params() {
	if [ $# -ne 6 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local wake_up_time=$2
	local flush_max_buffers=$3
	local staleness_time=$4
	local activity_threshold=$5
	local max_dirty_ratio=$6

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_cleaning Ocf_cache$i \
			--policy $policy \
			--alru-wake-up-time $wake_up_time \
			--alru-flush-max-buffers $flush_max_buffers \
			--alru-staleness-time $staleness_time \
			--alru-activity-threshold $activity_threshold \
			--alru-max-dirty-ratio $max_dirty_ratio
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_cleaning_acp_params() {
	if [ $# -ne 3 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local wake_up_time=$2
	local flush_max_buffers=$3

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_cleaning Ocf_cache$i \
			--policy $policy \
			--acp-wake-up-time $wake_up_time \
			--acp-flush-max-buffers $flush_max_buffers
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_cleaning_nop_params() {
	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_cleaning Ocf_cache$i \
			--policy $policy
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_seqcutoff_params() {
	if [ $# -ne 4 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local threshold=$2
	local promotion_count=$3
	local promote_on_threshold=$4

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_seqcutoff Ocf_core$i-1 \
			--policy $policy \
			--threshold $threshold \
			--promotion-count $promotion_count \
			--promote-on-threshold $promote_on_threshold
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_seqcutoff_params_all() {
	if [ $# -ne 4 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local threshold=$2
	local promotion_count=$3
	local promote_on_threshold=$4

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_seqcutoff Ocf_cache$i \
			--policy $policy \
			--threshold $threshold \
			--promotion-count $promotion_count \
			--promote-on-threshold $promote_on_threshold
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

__check_cache_mode() {
	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local cache_mode=$1

	$rpc_py bdev_ocf_get_bdevs | jq -e --arg cache_mode $cache_mode \
		'.caches[].cache_mode == $cache_mode'
}

__check_promotion_always_params() {
	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--arg policy $policy \
		'.caches[].promotion.policy == $policy'
}

__check_promotion_nhit_params() {
	if [ $# -ne 3 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local insertion_threshold=$2
	local trigger_threshold=$3

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--arg policy $policy \
		--argjson insertion_threshold $insertion_threshold \
		--argjson trigger_threshold $trigger_threshold \
		'.caches[].promotion |
		(.policy == $policy) and
		(.insertion_threshold == $insertion_threshold) and
		(.trigger_threshold == $trigger_threshold)'
}

__check_cleaning_alru_params() {
	if [ $# -ne 6 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local wake_up_time=$2
	local flush_max_buffers=$3
	local staleness_time=$4
	local activity_threshold=$5
	local max_dirty_ratio=$6

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--arg policy $policy \
		--argjson wake_up_time $wake_up_time \
		--argjson flush_max_buffers $flush_max_buffers \
		--argjson staleness_time $staleness_time \
		--argjson activity_threshold $activity_threshold \
		--argjson max_dirty_ratio $max_dirty_ratio \
		'.caches[].cleaning |
		(.policy == $policy) and
		(.wake_up_time == $wake_up_time) and
		(.flush_max_buffers == $flush_max_buffers) and
		(.staleness_time == $staleness_time) and
		(.activity_threshold == $activity_threshold) and
		(.max_dirty_ratio == $max_dirty_ratio)'
}

__check_cleaning_acp_params() {
	if [ $# -ne 3 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local wake_up_time=$2
	local flush_max_buffers=$3

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--arg policy $policy \
		--argjson wake_up_time $wake_up_time \
		--argjson flush_max_buffers $flush_max_buffers \
		'.caches[].cleaning |
		(.policy == $policy) and
		(.wake_up_time == $wake_up_time) and
		(.flush_max_buffers == $flush_max_buffers)'
}

__check_cleaning_nop_params() {
	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--arg policy $policy \
		'.caches[].cleaning.policy == $policy'
}

__check_seqcutoff_params() {
	if [ $# -ne 4 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local threshold=$2
	local promotion_count=$3
	if [ $4 -eq 0 ]; then
		local promote_on_threshold=false
	elif [ $4 -eq 1 ]; then
		local promote_on_threshold=true
	fi

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--arg policy $policy \
		--argjson threshold $threshold \
		--argjson promotion_count $promotion_count \
		--argjson promote_on_threshold $promote_on_threshold \
		'.caches[].cores[0].seq_cutoff |
		(.policy == $policy) and
		(.threshold == $threshold * 1024) and
		(.promotion_count == $promotion_count) and
		(.promote_on_threshold == $promote_on_threshold)'

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--arg policy $seqcutoff_policy_default \
		--argjson threshold $seqcutoff_threshold_default \
		--argjson promotion_count $seqcutoff_promotion_count_default \
		--argjson promote_on_threshold $seqcutoff_promote_on_threshold_default \
		'.caches[].cores[1].seq_cutoff |
		(.policy == $policy) and
		(.threshold == $threshold * 1024) and
		(.promotion_count == $promotion_count) and
		(.promote_on_threshold == $promote_on_threshold)'
}

__check_seqcutoff_params_all() {
	if [ $# -ne 4 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local policy=$1
	local threshold=$2
	local promotion_count=$3
	if [ $4 -eq 0 ]; then
		local promote_on_threshold=false
	elif [ $4 -eq 1 ]; then
		local promote_on_threshold=true
	fi

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--arg policy $policy \
		--argjson threshold $threshold \
		--argjson promotion_count $promotion_count \
		--argjson promote_on_threshold $promote_on_threshold \
		'.caches[].cores[].seq_cutoff |
		(.policy == $policy) and
		(.threshold == $threshold * 1024) and
		(.promotion_count == $promotion_count) and
		(.promote_on_threshold == $promote_on_threshold)'
}

# cache modes:

for add_cores in false true; do
	for create_caches in false true; do
		for stop_caches in false true; do
			start_spdk
			start_caches
			if [ $create_caches = true ]; then
				create_caches
				__check_caches_attached
				if [ $add_cores = true ]; then
					create_cores
					add_cores
					__check_cores_attached
				fi
			else
				__check_caches_detached
			fi
			for cache_mode in "${cache_modes[@]}"; do
				set_cache_mode $cache_mode
				__check_cache_mode $cache_mode
			done
			if [ $stop_caches = true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
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
			if [ $add_cores = true ]; then
				create_cores
				add_cores
				__check_cores_attached
			fi
			if [ $promotion_policy = always ]; then
				set_promotion_always_params $promotion_policy
				__check_promotion_always_params $promotion_policy
			elif [ $promotion_policy = nhit ]; then
				insertion_threshold=$(random_number ${promotion_nhit_insertion_threshold_range[0]} ${promotion_nhit_insertion_threshold_range[1]})
				trigger_threshold=$(random_number ${promotion_nhit_trigger_threshold_range[0]} ${promotion_nhit_trigger_threshold_range[1]})
				set_promotion_nhit_params $promotion_policy $insertion_threshold $trigger_threshold
				__check_promotion_nhit_params $promotion_policy $insertion_threshold $trigger_threshold
			fi
			if [ $stop_caches = true ]; then
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
			if [ $add_cores = true ]; then
				create_cores
				add_cores
				__check_cores_attached
			fi
			if [ $cleaning_policy = alru ]; then
				wake_up_time=$(random_number ${cleaning_alru_wake_up_time_range[0]} ${cleaning_alru_wake_up_time_range[1]})
				flush_max_buffers=$(random_number ${cleaning_alru_flush_max_buffers_range[0]} ${cleaning_alru_flush_max_buffers_range[1]})
				staleness_time=$(random_number ${cleaning_alru_staleness_time_range[0]} ${cleaning_alru_staleness_time_range[1]})
				activity_threshold=$(random_number ${cleaning_alru_activity_threshold_range[0]} ${cleaning_alru_activity_threshold_range[1]})
				max_dirty_ratio=$(random_number ${cleaning_alru_max_dirty_ratio_range[0]} ${cleaning_alru_max_dirty_ratio_range[1]})
				set_cleaning_alru_params $cleaning_policy $wake_up_time $flush_max_buffers $staleness_time $activity_threshold $max_dirty_ratio
				__check_cleaning_alru_params $cleaning_policy $wake_up_time $flush_max_buffers $staleness_time $activity_threshold $max_dirty_ratio
			elif [ $cleaning_policy = acp ]; then
				wake_up_time=$(random_number ${cleaning_acp_wake_up_time_range[0]} ${cleaning_acp_wake_up_time_range[1]})
				flush_max_buffers=$(random_number ${cleaning_acp_flush_max_buffers_range[0]} ${cleaning_acp_flush_max_buffers_range[1]})
				set_cleaning_acp_params $cleaning_policy $wake_up_time $flush_max_buffers
				__check_cleaning_acp_params $cleaning_policy $wake_up_time $flush_max_buffers
			elif [ $cleaning_policy = nop ]; then
				set_cleaning_nop_params $cleaning_policy
				__check_cleaning_nop_params $cleaning_policy
			fi
			if [ $stop_caches = true ]; then
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
			if [ $device = core ]; then
				set_seqcutoff_params $seqcutoff_policy $threshold $promotion_count $promote_on_threshold
				__check_seqcutoff_params $seqcutoff_policy $threshold $promotion_count $promote_on_threshold
			elif [ $device = cache ]; then
				set_seqcutoff_params_all $seqcutoff_policy $threshold $promotion_count $promote_on_threshold
				__check_seqcutoff_params_all $seqcutoff_policy $threshold $promotion_count $promote_on_threshold
			fi
			if [ $stop_caches = true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done
