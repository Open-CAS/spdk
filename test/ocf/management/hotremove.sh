#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source "$rootdir/test/ocf/common.sh"

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

__check_cores_detached_only_first() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[0].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[0].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[1].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[1].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[2].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[2].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[0].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[0].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[1].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[1].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[2].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[2].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[0].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[0].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[1].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[1].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[2].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[2].loading | not'
}

__check_cores_detached_all_but_first() {
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[0].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[0].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[1].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[1].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[2].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[0].cores[2].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[0].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[0].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[1].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[1].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[2].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[1].cores[2].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores | length == 3'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[0].base_attached'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[0].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[1].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[1].loading | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[2].base_attached | not'
	$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[2].cores[2].loading | not'
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

# cores in waitlist:

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
destroy_cores
__check_cores_waitlist_detached
stop_spdk

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
destroy_cores
__check_cores_waitlist_detached
create_cores
__check_cores_waitlist_attached
stop_spdk

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
destroy_cores
__check_cores_waitlist_detached
remove_cores
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
destroy_cores
__check_cores_waitlist_detached
create_cores
__check_cores_waitlist_attached
remove_cores
__check_cores_waitlist_empty
stop_spdk

# cores in waitlist (destroying only first):

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_detached_only_first
stop_spdk

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_detached_only_first
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev$i-1 200 512
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_attached
stop_spdk

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_detached_only_first
remove_cores
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_detached_only_first
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev$i-1 200 512
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_attached
remove_cores
__check_cores_waitlist_empty
stop_spdk

# cores in waitlist (destroying all but first):

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_detached_all_but_first
stop_spdk

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_detached_all_but_first
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev$i-2 200 512
	$rpc_py bdev_malloc_create -b Core_dev$i-3 200 512
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_attached
stop_spdk

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_detached_all_but_first
remove_cores
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_detached_all_but_first
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev$i-2 200 512
	$rpc_py bdev_malloc_create -b Core_dev$i-3 200 512
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_cores_waitlist_attached
remove_cores
__check_cores_waitlist_empty
stop_spdk

# caches only:

start_spdk
create_caches
start_caches
__check_caches_attached
destroy_caches
__check_caches_detached
stop_spdk

start_spdk
create_caches
start_caches
__check_caches_attached
destroy_caches
__check_caches_detached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
start_caches
__check_caches_attached
destroy_caches
__check_caches_detached
create_caches
__check_caches_attached
stop_spdk

start_spdk
create_caches
start_caches
__check_caches_attached
destroy_caches
__check_caches_detached
create_caches
__check_caches_attached
stop_caches
__check_caches_empty
stop_spdk

# caches only (destroying only first):

start_spdk
create_caches
start_caches
__check_caches_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
stop_spdk

start_spdk
create_caches
start_caches
__check_caches_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
start_caches
__check_caches_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
$rpc_py bdev_malloc_create -b Cache_dev1 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
stop_spdk

start_spdk
create_caches
start_caches
__check_caches_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
$rpc_py bdev_malloc_create -b Cache_dev1 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
stop_caches
__check_caches_empty
stop_spdk

# caches only (destroying all but first):

start_spdk
create_caches
start_caches
__check_caches_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
stop_spdk

start_spdk
create_caches
start_caches
__check_caches_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
start_caches
__check_caches_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
$rpc_py bdev_malloc_create -b Cache_dev2 100 512
$rpc_py bdev_malloc_create -b Cache_dev3 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
stop_spdk

start_spdk
create_caches
start_caches
__check_caches_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
$rpc_py bdev_malloc_create -b Cache_dev2 100 512
$rpc_py bdev_malloc_create -b Cache_dev3 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
stop_caches
__check_caches_empty
stop_spdk

# hotremove caches:

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
__check_cores_attached
create_caches
__check_caches_attached
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
__check_cores_attached
create_caches
__check_caches_attached
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
__check_cores_attached
remove_cores
__check_caches_detached
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
__check_cores_attached
remove_cores
__check_caches_detached
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
__check_cores_attached
remove_cores
__check_caches_detached
__check_cores_empty
create_caches
__check_caches_attached
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
__check_cores_attached
remove_cores
__check_caches_detached
__check_cores_empty
create_caches
__check_caches_attached
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

# hotremove caches (destroying only first):

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
__check_cores_attached
$rpc_py bdev_malloc_create -b Cache_dev1 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
__check_cores_attached
$rpc_py bdev_malloc_create -b Cache_dev1 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
__check_cores_attached
remove_cores
__check_caches_detached_only_first
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
__check_cores_attached
remove_cores
__check_caches_detached_only_first
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
__check_cores_attached
remove_cores
__check_caches_detached_only_first
__check_cores_empty
$rpc_py bdev_malloc_create -b Cache_dev1 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev1
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_only_first
__check_cores_attached
remove_cores
__check_caches_detached_only_first
__check_cores_empty
$rpc_py bdev_malloc_create -b Cache_dev1 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

# hotremove caches (destroying all but first):

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
__check_cores_attached
$rpc_py bdev_malloc_create -b Cache_dev2 100 512
$rpc_py bdev_malloc_create -b Cache_dev3 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
__check_cores_attached
$rpc_py bdev_malloc_create -b Cache_dev2 100 512
$rpc_py bdev_malloc_create -b Cache_dev3 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
__check_cores_attached
remove_cores
__check_caches_detached_all_but_first
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
__check_cores_attached
remove_cores
__check_caches_detached_all_but_first
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
__check_cores_attached
remove_cores
__check_caches_detached_all_but_first
__check_cores_empty
$rpc_py bdev_malloc_create -b Cache_dev2 100 512
$rpc_py bdev_malloc_create -b Cache_dev3 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
$rpc_py bdev_malloc_delete Cache_dev2
$rpc_py bdev_malloc_delete Cache_dev3
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_detached_all_but_first
__check_cores_attached
remove_cores
__check_caches_detached_all_but_first
__check_cores_empty
$rpc_py bdev_malloc_create -b Cache_dev2 100 512
$rpc_py bdev_malloc_create -b Cache_dev3 100 512
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

# hotremove cores:

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_cores
__check_caches_attached
__check_cores_detached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_cores
__check_caches_attached
__check_cores_detached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_cores
__check_caches_attached
__check_cores_detached
create_cores
__check_caches_attached
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_cores
__check_caches_attached
__check_cores_detached
create_cores
__check_caches_attached
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_cores
__check_caches_attached
__check_cores_detached
remove_cores
__check_caches_attached
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_cores
__check_caches_attached
__check_cores_detached
remove_cores
__check_caches_attached
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

# hotremove cores (destroying only first):

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_only_first
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_only_first
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_only_first
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev$i-1 200 512
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_only_first
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev$i-1 200 512
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_only_first
remove_cores
__check_caches_attached
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_only_first
remove_cores
__check_caches_attached
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

# hotremove cores (destroying all but first):

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_all_but_first
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_all_but_first
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_all_but_first
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev$i-2 200 512
	$rpc_py bdev_malloc_create -b Core_dev$i-3 200 512
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_all_but_first
for i in {1..3}; do
	$rpc_py bdev_malloc_create -b Core_dev$i-2 200 512
	$rpc_py bdev_malloc_create -b Core_dev$i-3 200 512
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_all_but_first
remove_cores
__check_caches_attached
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-2
	$rpc_py bdev_malloc_delete Core_dev$i-3
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.'
__check_caches_attached
__check_cores_detached_all_but_first
remove_cores
__check_caches_attached
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

# hotremove caches and cores:

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
create_caches
__check_caches_attached
create_cores
__check_cores_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
create_caches
__check_caches_attached
create_cores
__check_cores_attached
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
remove_cores
__check_caches_detached
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
remove_cores
__check_caches_detached
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
remove_cores
__check_caches_detached
__check_cores_empty
create_caches
__check_caches_attached
__check_cores_empty
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
remove_cores
__check_caches_detached
__check_cores_empty
create_caches
__check_caches_attached
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk

# hotremove caches and cores (inverse create after destroy order)

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
create_cores
__check_cores_attached
create_caches
__check_caches_attached
stop_spdk

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
destroy_caches
__check_caches_detached
destroy_cores
__check_cores_detached
create_cores
__check_cores_attached
create_caches
__check_caches_attached
stop_caches
__check_caches_empty
stop_spdk
