#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../../..)
source "$rootdir/test/ocf/common.sh"

# incomplete caches:

start_spdk
start_caches
__check_caches_detached
stop_spdk

start_spdk
start_caches
__check_caches_detached
stop_caches
__check_caches_empty
stop_spdk

# incomplete cores:

start_spdk
add_cores
__check_cores_waitlist_detached
stop_spdk

start_spdk
add_cores
__check_cores_waitlist_detached
remove_cores
__check_cores_waitlist_empty
stop_spdk

# incomplete caches and incomplete cores:

start_spdk
start_caches
add_cores
__check_caches_detached
__check_cores_waitlist_detached
stop_spdk

start_spdk
start_caches
add_cores
__check_caches_detached
__check_cores_waitlist_detached
stop_caches
__check_caches_empty
__check_cores_waitlist_detached
stop_spdk

start_spdk
start_caches
add_cores
__check_caches_detached
__check_cores_waitlist_detached
remove_cores
__check_cores_waitlist_empty
stop_spdk

start_spdk
start_caches
add_cores
__check_caches_detached
__check_cores_waitlist_detached
remove_cores
__check_cores_waitlist_empty
stop_caches
__check_caches_empty
stop_spdk

# complete caches and incomplete cores:

start_spdk
create_caches
start_caches
add_cores
__check_caches_attached
__check_cores_empty
__check_cores_waitlist_detached
stop_spdk

start_spdk
create_caches
start_caches
add_cores
__check_caches_attached
__check_cores_empty
__check_cores_waitlist_detached
stop_caches
__check_caches_empty
__check_cores_waitlist_detached
stop_spdk

start_spdk
create_caches
start_caches
add_cores
__check_caches_attached
__check_cores_empty
__check_cores_waitlist_detached
remove_cores
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_caches
start_caches
add_cores
__check_caches_attached
__check_cores_empty
__check_cores_waitlist_detached
remove_cores
__check_cores_waitlist_empty
stop_caches
__check_caches_empty
stop_spdk

# incomplete caches and complete cores:

start_spdk
create_cores
start_caches
__check_caches_detached
add_cores
__check_cores_empty
__check_cores_waitlist_attached
stop_spdk

start_spdk
create_cores
start_caches
__check_caches_detached
add_cores
__check_cores_empty
__check_cores_waitlist_attached
stop_caches
__check_caches_empty
__check_cores_waitlist_attached
stop_spdk

start_spdk
create_cores
start_caches
add_cores
__check_caches_detached
__check_cores_empty
__check_cores_waitlist_attached
remove_cores
__check_cores_waitlist_empty
stop_spdk

start_spdk
create_cores
start_caches
add_cores
__check_caches_detached
__check_cores_empty
__check_cores_waitlist_attached
remove_cores
__check_cores_waitlist_empty
stop_caches
__check_caches_empty
stop_spdk
