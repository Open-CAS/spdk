#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../../..)
source "$rootdir/test/ocf/common.sh"

# start caches and shutdown app:

start_spdk
create_caches
start_caches
__check_caches_attached
stop_spdk

# add cores and shutdown app:

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
stop_spdk

# start and stop caches:

start_spdk
create_caches
start_caches
__check_caches_attached
stop_caches
stop_spdk

# add and remove cores:

start_spdk
create_cores
add_cores
__check_cores_waitlist_attached
remove_cores
__check_cores_waitlist_empty
stop_spdk

# start caches, add cores and shutdown app:

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
stop_spdk

# add cores, start caches and shutdown app:

start_spdk
create_caches
create_cores
add_cores
__check_cores_waitlist_attached
start_caches
__check_caches_attached
__check_cores_attached
__check_cores_waitlist_empty
stop_spdk

# start caches, add cores and stop caches:

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
stop_caches
__check_caches_empty
__check_cores_waitlist_empty
stop_spdk

# add cores, start caches and stop caches:

start_spdk
create_caches
create_cores
add_cores
__check_cores_waitlist_attached
start_caches
__check_caches_attached
__check_cores_attached
__check_cores_waitlist_empty
stop_caches
__check_caches_empty
__check_cores_waitlist_empty
stop_spdk

# restart all caches with cores:

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
stop_caches
__check_caches_empty
__check_cores_waitlist_empty
stop_spdk

# remove first core and shutdown app:

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_ocf_remove_core Ocf_core$i-1 Ocf_cache$i
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores | length == 2'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].base_attached'
stop_spdk

# remove all cores but first and shutdown app:

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
for i in {1..3}; do
	$rpc_py bdev_ocf_remove_core Ocf_core$i-2 Ocf_cache$i
	$rpc_py bdev_ocf_remove_core Ocf_core$i-3 Ocf_cache$i
done
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores | length == 1'
$rpc_py bdev_ocf_get_bdevs | jq -e '.caches[].cores[].base_attached'
stop_spdk

# remove all cores and shutdown app:

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
remove_cores
__check_cores_empty
stop_spdk

# remove all cores and stop all caches:

start_spdk
create_caches
create_cores
start_caches
add_cores
__check_caches_attached
__check_cores_attached
remove_cores
__check_cores_empty
stop_caches
__check_caches_empty
stop_spdk
