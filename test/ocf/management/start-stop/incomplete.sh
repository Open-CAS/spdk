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

for stop_caches in false true; do
	start_spdk
	start_caches
	__check_caches_detached
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done

# incomplete cores:

for remove_cores in false true; do
	start_spdk
	add_cores
	__check_cores_waitlist_detached
	if [ $remove_cores = true ]; then
		remove_cores
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# incomplete caches and incomplete cores:

for remove_cores in false true; do
for stop_caches in false true; do
	start_spdk
	start_caches
	add_cores
	__check_caches_detached
	__check_cores_waitlist_detached
	if [ $remove_cores = true ]; then
		remove_cores
		__check_cores_waitlist_empty
	fi
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
done

# complete caches and incomplete cores:

for remove_cores in false true; do
for stop_caches in false true; do
	start_spdk
	create_caches
	start_caches
	add_cores
	__check_caches_attached
	__check_cores_empty
	__check_cores_waitlist_detached
	if [ $remove_cores = true ]; then
		remove_cores
		__check_cores_waitlist_empty
	fi
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
done

# incomplete caches and complete cores:

for remove_cores in false true; do
for stop_caches in false true; do
	start_spdk
	create_cores
	start_caches
	add_cores
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_attached
	if [ $remove_cores = true ]; then
		remove_cores
		__check_cores_waitlist_empty
	fi
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
done
