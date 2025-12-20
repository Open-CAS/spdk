#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../../..")
source "$rootdir/test/ocf/common.sh"

# start caches:

for stop_caches in false true; do
	start_spdk
	create_caches
	__check_caches_base_not_claimed
	__check_caches_empty
	__check_cores_waitlist_empty
	start_caches
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

# add cores:

for remove_cores in false true; do
	start_spdk
	create_cores
	__check_caches_empty
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	add_cores
	__check_caches_empty
	__check_cores_base_claimed
	__check_cores_waitlist_attached
	if [ $remove_cores == true ]; then
		remove_cores
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# start caches, then add cores:

for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	__check_caches_base_not_claimed
	__check_caches_empty
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	start_caches
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	__check_cores_empty
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

# add cores, then start caches:

for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	__check_caches_base_not_claimed
	__check_caches_empty
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	add_cores
	__check_caches_base_not_claimed
	__check_caches_empty
	__check_cores_base_claimed
	__check_cores_waitlist_attached
	start_caches
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

# restart all caches with cores:

for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	add_cores
	stop_caches
	__check_caches_base_not_claimed
	__check_caches_empty
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	start_caches
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
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

# remove cores:

for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	add_cores
	remove_cores
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_base_not_claimed
	__check_cores_waitlist_empty
	__check_cores_empty
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# remove first core:

for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	add_cores
	for i in {1..3}; do
		$rpc_py bdev_ocf_remove_core Ocf_core$i-1
	done
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_waitlist_empty
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Core_dev.-1"))] | any(.claimed) | not'
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Core_dev.-[23]"))] | all(.claimed)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores | length] | all(. == 2)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores_count] | all(. == 2)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | all(.base_attached)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | any(.loading) | not'
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# remove all cores but first:

for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	add_cores
	for i in {1..3}; do
		$rpc_py bdev_ocf_remove_core Ocf_core$i-2
		$rpc_py bdev_ocf_remove_core Ocf_core$i-3
	done
	__check_caches_base_claimed
	__check_caches_attached
	__check_cores_waitlist_empty
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Core_dev.-1"))] | all(.claimed)'
	$rpc_py bdev_get_bdevs | jq -e '[.[] | select(.name | test("Core_dev.-[23]"))] | any(.claimed) | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores | length] | all(. == 1)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores_count] | all(. == 1)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | all(.base_attached)'
	$rpc_py bdev_ocf_get_bdevs | jq -e '[.caches[].cores[]] | any(.loading) | not'
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_base_not_claimed
		__check_caches_empty
		__check_cores_base_not_claimed
		__check_cores_waitlist_empty
	fi
	stop_spdk
done
