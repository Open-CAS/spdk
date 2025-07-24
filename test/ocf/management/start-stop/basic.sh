#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../../..)
source "$rootdir/test/ocf/common.sh"

# start caches:

for stop_caches in false true; do
	start_spdk
	create_caches
	start_caches
	__check_caches_attached
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done

# add cores:

for remove_cores in false true; do
	start_spdk
	create_cores
	add_cores
	__check_cores_waitlist_attached
	if [ $remove_cores = true ]; then
		remove_cores
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# start caches, then add cores:

for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	add_cores
	__check_caches_attached
	__check_cores_attached
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# add cores, then start caches:

for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	add_cores
	__check_cores_waitlist_attached
	start_caches
	__check_caches_attached
	__check_cores_attached
	__check_cores_waitlist_empty
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
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
	__check_caches_attached
	__check_cores_attached
	stop_caches
	__check_caches_empty
	start_caches
	__check_caches_attached
	add_cores
	__check_cores_attached
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
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
	__check_caches_attached
	__check_cores_attached
	remove_cores
	__check_cores_empty
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
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
	__check_caches_attached
	__check_cores_attached
	for i in {1..3}; do
		$rpc_py bdev_ocf_remove_core Ocf_core$i-1
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores | length == 2'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores_count == 2'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].base_attached'
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
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
	__check_caches_attached
	__check_cores_attached
	for i in {1..3}; do
		$rpc_py bdev_ocf_remove_core Ocf_core$i-2
		$rpc_py bdev_ocf_remove_core Ocf_core$i-3
	done
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores | length == 1'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores_count == 1'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].base_attached'
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
