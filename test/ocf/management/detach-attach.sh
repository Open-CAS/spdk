#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source "$rootdir/test/ocf/common.sh"

# caches only:

for attach_caches in false true; do
for stop_caches in false true; do
	start_spdk
	create_caches
	start_caches
	__check_caches_attached
	detach_caches
	__check_caches_detached
	if [ $attach_caches = true ]; then
		attach_caches
		__check_caches_attached
	fi
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
done

# add cores before detaching caches:

for attach_caches in false true; do
for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	add_cores
	__check_caches_attached
	__check_cores_attached
	detach_caches
	__check_caches_detached
	__check_cores_attached
	if [ $attach_caches = true ]; then
		attach_caches
		__check_caches_attached
		__check_cores_attached
	fi
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
done

# add cores after detaching caches:

for attach_caches in false true; do
for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	__check_caches_attached
	detach_caches
	__check_caches_detached
	add_cores
	__check_caches_detached
	__check_cores_waitlist_attached
	if [ $attach_caches = true ]; then
		attach_caches
		__check_caches_attached
		__check_cores_attached
		__check_cores_waitlist_empty
	fi
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
done

# add cores before and remove after detaching caches:

for attach_caches in false true; do
for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	add_cores
	__check_caches_attached
	__check_cores_attached
	detach_caches
	__check_caches_detached
	__check_cores_attached
	remove_cores
	__check_caches_detached
	__check_cores_empty
	if [ $attach_caches = true ]; then
		attach_caches
		__check_caches_attached
		__check_cores_empty
	fi
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
done

# add cores after detaching caches and remove before/without attaching:

for attach_caches in false true; do
for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	__check_caches_attached
	detach_caches
	__check_caches_detached
	add_cores
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_attached
	remove_cores
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_empty
	if [ $attach_caches = true ]; then
		attach_caches
		__check_caches_attached
		__check_cores_empty
		__check_cores_waitlist_empty
	fi
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
done

# add cores after detaching caches and remove after attaching:

for stop_caches in false true; do
	start_spdk
	create_caches
	create_cores
	start_caches
	__check_caches_attached
	detach_caches
	__check_caches_detached
	add_cores
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_attached
	attach_caches
	__check_caches_attached
	__check_cores_attached
	__check_cores_waitlist_empty
	remove_cores
	__check_caches_attached
	__check_cores_empty
	__check_cores_waitlist_empty
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
