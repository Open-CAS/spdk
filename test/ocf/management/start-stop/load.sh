#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../../..")
source "$rootdir/test/ocf/common.sh"

# load caches without cores in metadata:

for add_cores in false true; do
	for stop_caches in false true; do
		start_spdk
		create_caches_with_metadata
		__check_caches_base_not_claimed
		create_cores
		__check_cores_base_not_claimed
		start_caches_try_load
		__check_caches_base_claimed
		__check_caches_attached
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
		__check_cores_empty
		if [ $add_cores == true ]; then
			add_cores
			__check_caches_base_claimed
			__check_caches_attached
			__check_cores_base_claimed
			__check_cores_waitlist_empty
			__check_cores_attached
		fi
		if [ $stop_caches == true ]; then
			stop_caches
			__check_caches_base_not_claimed
			__check_caches_empty
			__check_cores_base_not_claimed
			__check_cores_waitlist_empty
		fi
		stop_spdk
	done
done

# load caches with cores in metadata:

for add_cores in false true; do
	for stop_caches in false true; do
		start_spdk
		create_caches_with_metadata_with_cores
		__check_caches_base_not_claimed
		create_cores
		__check_cores_base_not_claimed
		start_caches_try_load
		__check_caches_base_claimed
		__check_caches_attached
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
		__check_cores_loading
		if [ $add_cores == true ]; then
			add_cores
			__check_caches_base_claimed
			__check_caches_attached
			__check_cores_base_claimed
			__check_cores_waitlist_empty
			__check_cores_attached
		fi
		if [ $stop_caches == true ]; then
			stop_caches
			__check_caches_base_not_claimed
			__check_caches_empty
			__check_cores_base_not_claimed
			__check_cores_waitlist_empty
		fi
		stop_spdk
	done
done

# load caches with cores in metadata (add cores first):

for create_cores in false true; do
	for stop_caches in false true; do
		start_spdk
		create_caches_with_metadata_with_cores
		add_cores
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_waitlist_detached
		start_caches_try_load
		__check_caches_base_claimed
		__check_caches_attached
		__check_cores_waitlist_detached
		__check_cores_loading
		if [ $create_cores == true ]; then
			create_cores
			__check_caches_base_claimed
			__check_caches_attached
			__check_cores_base_claimed
			__check_cores_waitlist_empty
			__check_cores_attached
		fi
		if [ $stop_caches == true ]; then
			stop_caches
			__check_caches_base_not_claimed
			__check_caches_empty
			if [ $create_cores == true ]; then
				__check_cores_base_not_claimed
				__check_cores_waitlist_empty
			else
				__check_cores_waitlist_detached
			fi
		fi
		stop_spdk
	done
done

# examine found cache device with metadata (without cores in metadata):

for stop_caches in false true; do
	start_spdk
	create_caches_persistent_with_metadata
	__check_caches_base_not_claimed
	destroy_caches_persistent
	start_caches_try_load
	__check_caches_detached
	__check_cores_waitlist_empty
	create_caches_persistent
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_waitlist_empty
	__check_cores_empty
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# examine found cache device with metadata (with cores in metadata):

for stop_caches in false true; do
	start_spdk
	create_caches_persistent_with_metadata_with_cores
	__check_caches_base_not_claimed
	create_cores
	__check_cores_base_not_claimed
	destroy_caches_persistent
	__check_cores_base_not_claimed
	start_caches_try_load
	__check_caches_detached
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	__check_cores_empty
	create_caches_persistent
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	__check_cores_loading
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# examine found cache device with metadata (with cores in metadata, added at different stages):

for stop_caches in false true; do
	start_spdk
	create_caches_persistent_with_metadata_with_cores
	__check_caches_base_not_claimed
	create_cores
	__check_cores_base_not_claimed
	destroy_caches_persistent
	__check_cores_base_not_claimed
	start_caches_try_load
	__check_caches_detached
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	__check_cores_empty
	create_caches_persistent
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	__check_cores_loading
	add_cores
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_claimed
	__check_cores_waitlist_empty
	__check_cores_attached
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

for stop_caches in false true; do
	start_spdk
	create_caches_persistent_with_metadata_with_cores
	__check_caches_base_not_claimed
	create_cores
	__check_cores_base_not_claimed
	destroy_caches_persistent
	__check_cores_base_not_claimed
	start_caches_try_load
	__check_caches_detached
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	__check_cores_empty
	add_cores
	__check_caches_detached
	__check_cores_base_claimed
	__check_cores_waitlist_attached
	__check_cores_empty
	create_caches_persistent
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_claimed
	__check_cores_waitlist_empty
	__check_cores_attached
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

for stop_caches in false true; do
	start_spdk
	create_caches_persistent_with_metadata_with_cores
	__check_caches_base_not_claimed
	create_cores
	__check_cores_base_not_claimed
	destroy_caches_persistent
	__check_cores_base_not_claimed
	add_cores
	__check_caches_empty
	__check_cores_base_claimed
	__check_cores_waitlist_attached
	start_caches_try_load
	__check_caches_detached
	__check_cores_base_claimed
	__check_cores_waitlist_attached
	__check_cores_empty
	create_caches_persistent
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_claimed
	__check_cores_waitlist_empty
	__check_cores_attached
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# examine found core device after cache load (without cores in metadata):

for stop_caches in false true; do
	start_spdk
	create_caches_with_metadata
	__check_caches_base_not_claimed
	start_caches_try_load
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_waitlist_empty
	__check_cores_empty
	add_cores
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_waitlist_detached
	__check_cores_empty
	create_cores
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_claimed
	__check_cores_waitlist_empty
	__check_cores_attached
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# examine found core device after cache load (with cores in metadata):

for stop_caches in false true; do
	start_spdk
	create_caches_with_metadata_with_cores
	__check_caches_base_not_claimed
	start_caches_try_load
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_waitlist_empty
	__check_cores_loading
	add_cores
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_waitlist_detached
	__check_cores_loading
	create_cores
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_claimed
	__check_cores_waitlist_empty
	__check_cores_attached
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# examine found cache with metadata first and then core devices after cache load:

for stop_caches in false true; do
	start_spdk
	create_caches_persistent_with_metadata_with_cores
	__check_caches_base_not_claimed
	destroy_caches_persistent
	add_cores
	__check_caches_empty
	__check_cores_waitlist_detached
	start_caches_try_load
	__check_caches_detached
	__check_cores_waitlist_detached
	__check_cores_empty
	create_caches_persistent
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_waitlist_detached
	__check_cores_loading
	create_cores
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_claimed
	__check_cores_waitlist_empty
	__check_cores_attached
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# examine found core devices first and then cache with metadata after cache load:

for stop_caches in false true; do
	start_spdk
	create_caches_persistent_with_metadata_with_cores
	__check_caches_base_not_claimed
	destroy_caches_persistent
	add_cores
	__check_caches_empty
	__check_cores_waitlist_detached
	start_caches_try_load
	__check_caches_detached
	__check_cores_waitlist_detached
	__check_cores_empty
	create_cores
	__check_caches_detached
	__check_cores_base_claimed
	__check_cores_waitlist_attached
	__check_cores_empty
	create_caches_persistent
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_claimed
	__check_cores_waitlist_empty
	__check_cores_attached
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# partially found cache and core devices:

for stop_caches in false true; do
	start_spdk
	create_caches_persistent_with_metadata_with_cores
	__check_caches_base_not_claimed
	destroy_caches_persistent
	add_cores
	__check_caches_empty
	__check_cores_waitlist_detached
	start_caches_try_load
	__check_caches_detached
	__check_cores_waitlist_detached
	__check_cores_empty

	persistent_cache_create Cache_dev2 "${persistent_cache_addr[1]}"
	persistent_cache_create Cache_dev3 "${persistent_cache_addr[2]}"
	waitforcondition ocf_settled
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Cache_dev[23]"))] | all(.claimed)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores | length == 0'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores_count == 0'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores_count == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | any(.base_attached) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | all(.loading)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores_count == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | any(.base_attached) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | all(.loading)'
	__check_cores_waitlist_detached

	for i in {1..3}; do
		$rpc_py bdev_malloc_create -b Core_dev2-$i 200 512
	done
	waitforcondition ocf_settled
	$rpc_py bdev_ocf_get_bdevs | jq -e '.'
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Cache_dev[23]"))] | all(.claimed)'
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Core_dev2-."))] | all(.claimed)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores | length == 0'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores_count == 0'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores_count == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | all(.base_attached)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | any(.loading) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores_count == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | any(.base_attached) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | all(.loading)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 6'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | any(.base_attached) | not'

	if [ $stop_caches == true ]; then
		stop_caches
		$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Cache_dev[23]"))] | any(.claimed) | not'
		$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Core_dev2-."))] | any(.claimed) | not'
		__check_caches_empty
		$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 6'
		$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | any(.base_attached) | not'
	fi
	stop_spdk
done
