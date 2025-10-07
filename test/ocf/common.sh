#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

source "$rootdir/test/common/autotest_common.sh"

rpc_py="$rootdir/scripts/rpc.py"

cache_line_sizes=(4 8 16 32 64)
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

###### Change those depending on persistent storage availability:
persistent_cache_addr=(05:00.0 06:00.0 08:00.0)

persistent_cache_create() {
	# $1: name of bdev to create
	# $2: address/path/identifier of specific device

	if [ $# -ne 2 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local name=$1
	local addr=$2

	$rpc_py bdev_virtio_attach_controller -t pci -a $addr -d blk $name
}

persistent_cache_destroy() {
	# $1: name of bdev to destroy

	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local name=$1

	$rpc_py bdev_virtio_detach_controller $name
}
######

malloc_cache_create() {
	# $1: name of bdev to create

	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local name=$1

	$rpc_py bdev_malloc_create -b $name 100 512
}

malloc_cache_destroy() {
	# $1: name of bdev to destroy

	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local name=$1

	$rpc_py bdev_malloc_delete $name
}

malloc_core_create() {
	# $1: name of bdev to create

	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local name=$1

	$rpc_py bdev_malloc_create -b $name 200 512
}

malloc_core_destroy() {
	malloc_cache_destroy "$@"
}

start_spdk() {
	"$SPDK_BIN_DIR/spdk_tgt" -L vbdev_ocf "$@" &
	spdk_pid=$!
	trap 'killprocess $spdk_pid; exit 1' SIGINT SIGTERM EXIT
	waitforlisten $spdk_pid
}

stop_spdk() {
	trap - SIGINT SIGTERM EXIT
	killprocess $spdk_pid
}

ocf_settled() {
	# Check if there are no caches currently busy (under a lock).
	# Instead of the usual cache info, an empty JSON object is printed for such cache.
	if ! ./scripts/rpc.py bdev_ocf_get_bdevs | jq -e \
		'.caches | any(. == {}) | not'; then

		return 1
	fi

	# Check if there are no cores in caches still in the initialization process.
	# Such condition can happen when base bdevs with OCF metadata were found for
	# started caches and their configuration is loaded from this metadata in background.
	if ! ./scripts/rpc.py bdev_ocf_get_bdevs | jq -e \
		'if (.caches | length == 0) then true
			else ([.caches[].cores[]] | any(. == {}) | not) end'; then

		return 1
	fi

	# By using array arithmetic check if there is no attached
	# cores in wait list that belong to any attached cache.
	# Those cores should be automatically added to their caches, so if there
	# are any cores left it means that the adding process did not finish yet.
	# Such condition can happen when base bdevs were found for started caches
	# (or added cores in wait list) and then attaching caches and moving cores
	# from wait list is done in background.
	if ! ./scripts/rpc.py bdev_ocf_get_bdevs | jq -e \
		'([.cores_waitlist[] | select(.base_attached).cache_name] | unique) -
			(([.cores_waitlist[] | select(.base_attached).cache_name] | unique) -
			([.caches[] | select(.base_attached).name])) == []'; then

		return 1
	fi
}

create_caches_do() {
	for i in {1..3}; do
		malloc_cache_create Cache_dev$i
	done
}

create_caches() {
	create_caches_do

	# Give it some time to settle before returning, as there might be
	# some cores in wait list that need to be moved to attached caches.
	waitforcondition ocf_settled

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_caches_with_metadata() {
	create_caches
	start_caches
	stop_caches
}

create_caches_with_metadata_with_cores() {
	create_caches
	create_cores
	start_caches
	add_cores
	stop_caches
	destroy_cores
}

destroy_caches() {
	for i in {1..3}; do
		malloc_cache_destroy Cache_dev$i
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_caches_only_first() {
	malloc_cache_create Cache_dev1
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

destroy_caches_only_first() {
	malloc_cache_destroy Cache_dev1
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_caches_all_but_first() {
	malloc_cache_create Cache_dev2
	malloc_cache_create Cache_dev3
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

destroy_caches_all_but_first() {
	malloc_cache_destroy Cache_dev2
	malloc_cache_destroy Cache_dev3
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_caches_persistent_do() {
	for i in {1..3}; do
		persistent_cache_create Cache_dev$i "${persistent_cache_addr[i - 1]}"
	done
}

create_caches_persistent() {
	create_caches_persistent_do

	# Give it a bit more time to settle, because this might be called after
	# caches load with attached cores in wait list and it may take more time
	# to load caches configuration and then move all cores to loaded caches.
	waitforcondition ocf_settled 20

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_caches_persistent_with_metadata() {
	create_caches_persistent
	start_caches
	stop_caches
}

create_caches_persistent_with_metadata_with_cores() {
	create_caches_persistent
	create_cores
	start_caches
	add_cores
	stop_caches
	destroy_cores
}

destroy_caches_persistent() {
	for i in {1..3}; do
		persistent_cache_destroy Cache_dev$i
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_cores_do() {
	for i in {1..3}; do
		for j in {1..3}; do
			malloc_core_create Core_dev$i-$j
		done
	done
}

create_cores() {
	create_cores_do

	# Give it some time to settle before returning, as there might be some cores
	# in wait list that need to be moved to their caches after attaching bases.
	waitforcondition ocf_settled

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

destroy_cores() {
	for i in {1..3}; do
		for j in {1..3}; do
			malloc_core_destroy Core_dev$i-$j
		done
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_cores_only_first() {
	for i in {1..3}; do
		malloc_core_create Core_dev$i-1
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

destroy_cores_only_first() {
	for i in {1..3}; do
		malloc_core_destroy Core_dev$i-1
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_cores_all_but_first() {
	for i in {1..3}; do
		malloc_core_create Core_dev$i-2
		malloc_core_create Core_dev$i-3
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

destroy_cores_all_but_first() {
	for i in {1..3}; do
		malloc_core_destroy Core_dev$i-2
		malloc_core_destroy Core_dev$i-3
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

start_caches_do() {
	for i in {1..3}; do
		$rpc_py bdev_ocf_start_cache Ocf_cache$i Cache_dev$i "$@"
	done
}

start_caches() {
	start_caches_do --no-load

	# Give it some time to settle before returning, as there might be
	# some cores in wait list that need to be moved to attached caches.
	waitforcondition ocf_settled

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

start_caches_try_load() {
	start_caches_do

	# Give it some time to settle before returning, as there might be
	# some cores in wait list that need to be moved to attached caches.
	waitforcondition ocf_settled

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

start_caches_with_cache_line_size() {
	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local cache_line_size=$1

	start_caches_do --cache-line-size $cache_line_size --no-load

	# Give it some time to settle before returning, as there might be
	# some cores in wait list that need to be moved to attached caches.
	waitforcondition ocf_settled

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

stop_caches() {
	for i in {1..3}; do
		$rpc_py bdev_ocf_stop_cache Ocf_cache$i
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

detach_caches() {
	for i in {1..3}; do
		$rpc_py bdev_ocf_detach_cache Ocf_cache$i
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

attach_caches() {
	for i in {1..3}; do
		$rpc_py bdev_ocf_attach_cache Ocf_cache$i Cache_dev$i
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

add_cores_do() {
	for i in {1..3}; do
		for j in {1..3}; do
			$rpc_py bdev_ocf_add_core Ocf_core$i-$j Core_dev$i-$j Ocf_cache$i
		done
	done
}

add_cores() {
	add_cores_do

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

remove_cores() {
	for i in {1..3}; do
		for j in {1..3}; do
			$rpc_py bdev_ocf_remove_core Ocf_core$i-$j
		done
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

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
	for i in {1..3}; do
		$rpc_py bdev_ocf_set_promotion Ocf_cache$i \
			--policy always
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_promotion_nhit_params() {
	if [ $# -ne 2 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local insertion_threshold=$1
	local trigger_threshold=$2

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_promotion Ocf_cache$i \
			--policy nhit \
			--nhit-insertion-threshold $insertion_threshold \
			--nhit-trigger-threshold $trigger_threshold
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_cleaning_alru_params() {
	if [ $# -ne 5 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local wake_up_time=$1
	local flush_max_buffers=$2
	local staleness_time=$3
	local activity_threshold=$4
	local max_dirty_ratio=$5

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_cleaning Ocf_cache$i \
			--policy alru \
			--alru-wake-up-time $wake_up_time \
			--alru-flush-max-buffers $flush_max_buffers \
			--alru-staleness-time $staleness_time \
			--alru-activity-threshold $activity_threshold \
			--alru-max-dirty-ratio $max_dirty_ratio
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_cleaning_acp_params() {
	if [ $# -ne 2 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local wake_up_time=$1
	local flush_max_buffers=$2

	for i in {1..3}; do
		$rpc_py bdev_ocf_set_cleaning Ocf_cache$i \
			--policy acp \
			--acp-wake-up-time $wake_up_time \
			--acp-flush-max-buffers $flush_max_buffers
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

set_cleaning_nop_params() {
	for i in {1..3}; do
		$rpc_py bdev_ocf_set_cleaning Ocf_cache$i \
			--policy nop
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

__check_caches_base_claimed() {
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Cache_dev"))] | all(.claimed)'
}

__check_caches_base_not_claimed() {
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Cache_dev"))] | any(.claimed) | not'
}

__check_caches_empty() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 0'
}

__check_caches_attached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | all(.base_attached)'
}

__check_caches_detached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | any(.base_attached) | not'
}

__check_caches_detached_only_first() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].base_attached'
}

__check_caches_detached_all_but_first() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].base_attached | not'
}

__check_cores_base_claimed() {
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Core_dev"))] | all(.claimed)'
}

__check_cores_base_not_claimed() {
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Core_dev"))] | any(.claimed) | not'
}

__check_cores_empty() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores | length] | all(. == 0)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores_count] | all(. == 0)'
}

__check_cores_loading() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores | length] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores_count] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | any(.base_attached) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | all(.loading)'
}

__check_cores_attached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores | length] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores_count] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | all(.base_attached)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | any(.loading) | not'
}

__check_cores_detached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores | length] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores_count] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | any(.base_attached) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | any(.loading) | not'
}

__check_cores_detached_only_first() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores | length] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores_count] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[0]] | any(.base_attached) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[0]] | any(.loading) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[1]] | all(.base_attached)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[1]] | any(.loading) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[2]] | all(.base_attached)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[2]] | any(.loading) | not'
}

__check_cores_detached_all_but_first() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores | length] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores_count] | all(. == 3)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[0]] | all(.base_attached)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[0]] | any(.loading) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[1]] | any(.base_attached) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[1]] | any(.loading) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[2]] | any(.base_attached) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[2]] | any(.loading) | not'
}

__check_cores_waitlist_empty() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 0'
}

__check_cores_waitlist_attached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 9'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | all(.base_attached)'
}

__check_cores_waitlist_detached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 9'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | any(.base_attached) | not'
}

__check_cores_waitlist_detached_only_first() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 9'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[0].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[1].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[2].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[3].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[4].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[5].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[6].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[7].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[8].base_attached'
}

__check_cores_waitlist_detached_all_but_first() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 9'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[0].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[1].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[2].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[3].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[4].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[5].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[6].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[7].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[8].base_attached | not'
}

__check_setup_completed() {
	for i in {1..3}; do
		for j in {1..3}; do
			waitforbdev Core_dev$i-$j 20000
		done
	done

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_claimed
	__check_cores_waitlist_empty
	__check_cores_attached
}

__check_cache_line_size() {
	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local cache_line_size=$1

	$rpc_py bdev_ocf_get_bdevs | jq -e --argjson cache_line_size $cache_line_size \
		'.caches | all(.cache_line_size == $cache_line_size * 1024)'
}

__check_cache_mode() {
	if [ $# -ne 1 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local cache_mode=$1

	$rpc_py bdev_ocf_get_bdevs | jq -e --arg cache_mode $cache_mode \
		'.caches | all(.cache_mode == $cache_mode)'
}

__check_promotion_always_params() {
	$rpc_py bdev_ocf_get_bdevs | jq -e \
		'.caches | all(.promotion.policy == "always")'
}

__check_promotion_nhit_params() {
	if [ $# -ne 2 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local insertion_threshold=$1
	local trigger_threshold=$2

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--argjson insertion_threshold $insertion_threshold \
		--argjson trigger_threshold $trigger_threshold \
		'[.caches[].promotion] | all(
		(.policy == "nhit") and
		(.insertion_threshold == $insertion_threshold) and
		(.trigger_threshold == $trigger_threshold))'
}

__check_cleaning_alru_params() {
	if [ $# -ne 5 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local wake_up_time=$1
	local flush_max_buffers=$2
	local staleness_time=$3
	local activity_threshold=$4
	local max_dirty_ratio=$5

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--argjson wake_up_time $wake_up_time \
		--argjson flush_max_buffers $flush_max_buffers \
		--argjson staleness_time $staleness_time \
		--argjson activity_threshold $activity_threshold \
		--argjson max_dirty_ratio $max_dirty_ratio \
		'[.caches[].cleaning] | all(
		(.policy == "alru") and
		(.wake_up_time == $wake_up_time) and
		(.flush_max_buffers == $flush_max_buffers) and
		(.staleness_time == $staleness_time) and
		(.activity_threshold == $activity_threshold) and
		(.max_dirty_ratio == $max_dirty_ratio))'
}

__check_cleaning_acp_params() {
	if [ $# -ne 2 ]; then
		echo >&2 "invalid number of arguments"
		exit 1
	fi

	local wake_up_time=$1
	local flush_max_buffers=$2

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--argjson wake_up_time $wake_up_time \
		--argjson flush_max_buffers $flush_max_buffers \
		'[.caches[].cleaning] | all(
		(.policy == "acp") and
		(.wake_up_time == $wake_up_time) and
		(.flush_max_buffers == $flush_max_buffers))'
}

__check_cleaning_nop_params() {
	$rpc_py bdev_ocf_get_bdevs | jq -e \
		'.caches | all(.cleaning.policy == "nop")'
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
		'[.caches[].cores[0].seq_cutoff] | all(
		(.policy == $policy) and
		(.threshold == $threshold * 1024) and
		(.promotion_count == $promotion_count) and
		(.promote_on_threshold == $promote_on_threshold))'

	$rpc_py bdev_ocf_get_bdevs | jq -e \
		--arg policy $seqcutoff_policy_default \
		--argjson threshold $seqcutoff_threshold_default \
		--argjson promotion_count $seqcutoff_promotion_count_default \
		--argjson promote_on_threshold $seqcutoff_promote_on_threshold_default \
		'[.caches[].cores[1].seq_cutoff] | all(
		(.policy == $policy) and
		(.threshold == $threshold * 1024) and
		(.promotion_count == $promotion_count) and
		(.promote_on_threshold == $promote_on_threshold))'
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
		'[.caches[].cores[].seq_cutoff] | all(
		(.policy == $policy) and
		(.threshold == $threshold * 1024) and
		(.promotion_count == $promotion_count) and
		(.promote_on_threshold == $promote_on_threshold))'
}

# Generate random number between range.
random_number() {
	# $1: lower range (inclusive)
	# $2: upper range (inclusive)
	# stdout: random number between range

	echo $(($1 + RANDOM % ($2 - $1 + 1)))
}

# Convert an array of items to comma separated list of items.
array_to_comma_list() {
	# $1: name of array variable
	# stdout: string with comma separated elements

	IFS=","
	declare -n array="$1"
	echo "${array[*]}"
}
