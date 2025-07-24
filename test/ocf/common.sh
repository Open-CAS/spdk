#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

source "$rootdir/test/common/autotest_common.sh"

rpc_py="$rootdir/scripts/rpc.py"
cache_dev_persistent_addr=(05:00.0 06:00.0 08:00.0)

start_spdk() {
	$SPDK_BIN_DIR/spdk_tgt "$@" &
	spdk_pid=$!
	trap 'killprocess $spdk_pid; exit 1' SIGINT SIGTERM EXIT
	waitforlisten $spdk_pid
}

stop_spdk() {
	trap - SIGINT SIGTERM EXIT
	killprocess $spdk_pid
}

create_caches() {
	for i in {1..3}; do
		$rpc_py bdev_malloc_create -b Cache_dev$i 100 512
	done

	# Give it some time to settle before checking, as there might be
	# some cores in waitlist that needs to be moved to started caches.
	sleep 3

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

destroy_caches() {
	for i in {1..3}; do
		$rpc_py bdev_malloc_delete Cache_dev$i
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_caches_persistent() {
	for i in {1..3}; do
		$rpc_py bdev_virtio_attach_controller -t pci -a "${cache_dev_persistent_addr[i-1]}" -d blk Cache_dev$i
	done

	# Give a bit more time to settle, because this will be called after
	# caches load with attached cores in waitlist and it may take more time
	# to move all of them to loaded caches.
	sleep 15

	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

destroy_caches_persistent() {
	for i in {1..3}; do
		$rpc_py bdev_virtio_detach_controller Cache_dev$i
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

create_cores() {
	for i in {1..3}; do
		for j in {1..3}; do
			$rpc_py bdev_malloc_create -b Core_dev$i-$j 200 512
		done
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

destroy_cores() {
	for i in {1..3}; do
		for j in {1..3}; do
			$rpc_py bdev_malloc_delete Core_dev$i-$j
		done
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

start_caches() {
	for i in {1..3}; do
		$rpc_py bdev_ocf_start_cache Ocf_cache$i Cache_dev$i --no-load
	done
	# executed before cores from waitlist are added
	sleep 3
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
}

start_caches_try_load() {
	for i in {1..3}; do
		$rpc_py bdev_ocf_start_cache Ocf_cache$i Cache_dev$i
	done
	# executed before cores from waitlist are added
	sleep 3
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

add_cores() {
	for i in {1..3}; do
		for j in {1..3}; do
			$rpc_py bdev_ocf_add_core Ocf_core$i-$j Core_dev$i-$j Ocf_cache$i
		done
	done
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

__check_caches_empty() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 0'
}

__check_caches_attached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].base_attached'
}

__check_caches_detached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].base_attached | not'
}

__check_cores_empty() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores | length == 0'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores_count == 0'
}

__check_cores_attached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores_count == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].loading | not'
}

__check_cores_detached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores_count == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].loading | not'
}

__check_cores_loading() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores_count == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].loading'
}

__check_cores_waitlist_empty() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 0'
}

__check_cores_waitlist_attached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 9'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[].base_attached'
}

__check_cores_waitlist_detached() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 9'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[].base_attached | not'
}

# Generate random number between range.
random_number() {
	# $1: lower range (inclusive)
	# $2: upper range (inclusive)
	# stdout: random number between range

	echo $(($1 + $RANDOM % ($2-$1+1)))
}

# Convert an array of items to comma separated list of items.
array_to_comma_list() {
	# $1: name of array variable
	# stdout: string with comma separated elements

	IFS=","
	declare -n array="$1"
	echo "${array[*]}"
}
