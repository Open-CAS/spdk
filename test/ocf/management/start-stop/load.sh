#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../../..)
source "$rootdir/test/ocf/common.sh"

create_caches_with_metadata() {
	create_caches
	start_caches
	__check_caches_attached
	stop_caches
	__check_caches_empty
}

create_caches_with_metadata_with_cores() {
	create_caches
	create_cores
	start_caches
	add_cores
	__check_caches_attached
	__check_cores_attached
	stop_caches
	__check_caches_empty
	destroy_cores
}

create_caches_with_metadata_persistent() {
	create_caches_persistent
	start_caches
	__check_caches_attached
	stop_caches
	__check_caches_empty
}

create_caches_with_metadata_with_cores_persistent() {
	create_caches_persistent
	create_cores
	start_caches
	add_cores
	__check_caches_attached
	__check_cores_attached
	stop_caches
	__check_caches_empty
	destroy_cores
}

# load caches without cores:

start_spdk
create_caches_with_metadata
start_caches_try_load
__check_caches_attached
stop_spdk

start_spdk
create_caches_with_metadata
start_caches_try_load
__check_caches_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata
create_cores
start_caches_try_load
__check_caches_attached
__check_cores_empty
add_cores
__check_cores_attached
stop_spdk

start_spdk
create_caches_with_metadata
create_cores
start_caches_try_load
__check_caches_attached
__check_cores_empty
add_cores
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

# load caches with cores:

start_spdk
create_caches_with_metadata_with_cores
start_caches_try_load
__check_caches_attached
__check_cores_loading
stop_spdk

start_spdk
create_caches_with_metadata_with_cores
start_caches_try_load
__check_caches_attached
__check_cores_loading
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores
create_cores
start_caches_try_load
__check_caches_attached
__check_cores_loading
stop_spdk

start_spdk
create_caches_with_metadata_with_cores
create_cores
start_caches_try_load
__check_caches_attached
__check_cores_loading
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores
create_cores
start_caches_try_load
__check_caches_attached
__check_cores_loading
add_cores
__check_cores_attached
stop_spdk

start_spdk
create_caches_with_metadata_with_cores
create_cores
start_caches_try_load
__check_caches_attached
__check_cores_loading
add_cores
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores
add_cores
__check_cores_waitlist_detached
start_caches_try_load
__check_caches_attached
__check_cores_loading
__check_cores_waitlist_detached
stop_spdk

start_spdk
create_caches_with_metadata_with_cores
add_cores
__check_cores_waitlist_detached
start_caches_try_load
__check_caches_attached
__check_cores_loading
__check_cores_waitlist_detached
stop_caches
__check_caches_empty
stop_spdk

# examine found cache device with metadata:

start_spdk
create_caches_with_metadata_persistent
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
create_caches_persistent
__check_caches_attached
stop_spdk

start_spdk
create_caches_with_metadata_persistent
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
create_caches_persistent
__check_caches_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
__check_cores_empty
create_caches_persistent
__check_caches_attached
__check_cores_loading
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
__check_cores_empty
create_caches_persistent
__check_caches_attached
__check_cores_loading
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
create_cores
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
__check_cores_empty
create_caches_persistent
__check_caches_attached
__check_cores_loading
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
create_cores
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
__check_cores_empty
create_caches_persistent
__check_caches_attached
__check_cores_loading
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
create_cores
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
__check_cores_empty
__check_cores_waitlist_empty
create_caches_persistent
__check_caches_attached
__check_cores_loading
__check_cores_waitlist_empty
add_cores
__check_cores_attached
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
create_cores
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
__check_cores_empty
__check_cores_waitlist_empty
create_caches_persistent
__check_caches_attached
__check_cores_loading
__check_cores_waitlist_empty
add_cores
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
create_cores
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
__check_cores_empty
__check_cores_waitlist_empty
add_cores
__check_cores_empty
__check_cores_waitlist_attached
create_caches_persistent
__check_caches_attached
__check_cores_attached
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
create_cores
destroy_caches_persistent
start_caches_try_load
__check_caches_detached
__check_cores_empty
__check_cores_waitlist_empty
add_cores
__check_cores_empty
__check_cores_waitlist_attached
create_caches_persistent
__check_caches_attached
__check_cores_attached
__check_cores_waitlist_empty
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
create_cores
destroy_caches_persistent
add_cores
__check_cores_waitlist_attached
start_caches_try_load
__check_caches_detached
__check_cores_empty
create_caches_persistent
__check_caches_attached
__check_cores_attached
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
create_cores
destroy_caches_persistent
add_cores
__check_cores_waitlist_attached
start_caches_try_load
__check_caches_detached
__check_cores_empty
create_caches_persistent
__check_caches_attached
__check_cores_attached
__check_cores_waitlist_empty
stop_caches
__check_caches_empty
stop_spdk

# examine found core after cache load:

start_spdk
create_caches_with_metadata
start_caches_try_load
__check_caches_attached
__check_cores_empty
add_cores
__check_cores_empty
__check_cores_waitlist_detached
create_cores
__check_cores_attached
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_caches_with_metadata
start_caches_try_load
__check_caches_attached
__check_cores_empty
add_cores
__check_cores_empty
__check_cores_waitlist_detached
create_cores
__check_cores_attached
__check_cores_waitlist_empty
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores
start_caches_try_load
__check_caches_attached
__check_cores_loading
add_cores
__check_cores_loading
__check_cores_waitlist_detached
create_cores
__check_cores_attached
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores
start_caches_try_load
__check_caches_attached
__check_cores_loading
add_cores
__check_cores_loading
__check_cores_waitlist_detached
create_cores
__check_cores_attached
__check_cores_waitlist_empty
stop_caches
__check_caches_empty
stop_spdk

# examine found cache with metadata and core after cache load:

start_spdk
create_caches_with_metadata_with_cores_persistent
destroy_caches_persistent
add_cores
__check_cores_waitlist_detached
start_caches_try_load
__check_caches_detached
__check_cores_empty
create_caches_persistent
__check_caches_attached
__check_cores_loading
__check_cores_waitlist_detached
create_cores
__check_cores_attached
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
destroy_caches_persistent
add_cores
__check_cores_waitlist_detached
start_caches_try_load
__check_caches_detached
__check_cores_empty
create_caches_persistent
__check_caches_attached
__check_cores_loading
__check_cores_waitlist_detached
create_cores
__check_cores_attached
__check_cores_waitlist_empty
stop_caches
__check_caches_empty
stop_spdk

# partially found cache and core devices:

start_spdk
create_caches_with_metadata_with_cores_persistent
destroy_caches_persistent
add_cores
__check_cores_waitlist_detached
start_caches_try_load
__check_caches_detached
__check_cores_empty
$rpc_py bdev_virtio_attach_controller -t pci -a "${cache_dev_persistent_addr[1]}" -d blk Cache_dev2
$rpc_py bdev_virtio_attach_controller -t pci -a "${cache_dev_persistent_addr[2]}" -d blk Cache_dev3
sleep 3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores | length == 0'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[].loading'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[].loading'
__check_cores_waitlist_detached
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev2-$i 200 512
done
sleep 3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores | length == 0'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[].loading | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[].loading'
$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 6'
$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[].base_attached | not'
stop_spdk

start_spdk
create_caches_with_metadata_with_cores_persistent
destroy_caches_persistent
add_cores
__check_cores_waitlist_detached
start_caches_try_load
__check_caches_detached
__check_cores_empty
$rpc_py bdev_virtio_attach_controller -t pci -a "${cache_dev_persistent_addr[1]}" -d blk Cache_dev2
$rpc_py bdev_virtio_attach_controller -t pci -a "${cache_dev_persistent_addr[2]}" -d blk Cache_dev3
sleep 3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores | length == 0'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[].loading'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[].loading'
__check_cores_waitlist_detached
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev2-$i 200 512
done
sleep 3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores | length == 0'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[].loading | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].base_attached'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | length == 3'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[].base_attached | not'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[].loading'
$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist | length == 6'
$rpc_py bdev_ocf_get_bdevs | jq -e '.cores_waitlist[].base_attached | not'
stop_caches
__check_caches_empty
stop_spdk
