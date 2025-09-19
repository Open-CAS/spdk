#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../../..)
source "$rootdir/test/ocf/common.sh"

# found cache device (cache only):

for stop_caches in false true; do
	start_spdk
	start_caches
	__check_caches_detached
	create_caches
	__check_caches_attached
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done

# found cache device (cache with core):

for stop_caches in false true; do
	start_spdk
	start_caches
	add_cores
	__check_caches_detached
	__check_cores_waitlist_detached
	create_caches
	__check_caches_attached
	__check_cores_waitlist_detached
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
		__check_cores_waitlist_detached
	fi
	stop_spdk
done

# found core device (cores only):

for remove_cores in false true; do
	start_spdk
	add_cores
	__check_cores_waitlist_detached
	create_cores
	__check_cores_waitlist_attached
	if [ $remove_cores = true ]; then
		remove_cores
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# found core device (cache with core):

for stop_caches in false true; do
	start_spdk
	start_caches
	add_cores
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_detached
	create_cores
	__check_cores_empty
	__check_cores_waitlist_attached
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
		__check_cores_waitlist_attached
	fi
	stop_spdk
done

# found core device (cache with core, inverse add order):

for stop_caches in false true; do
	start_spdk
	add_cores
	start_caches
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_detached
	create_cores
	__check_cores_empty
	__check_cores_waitlist_attached
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
		__check_cores_waitlist_attached
	fi
	stop_spdk
done

# found core device, then start cache:

for remove_cores in false true; do
	for stop_caches in false true; do
		start_spdk
		add_cores
		__check_cores_waitlist_detached
		create_cores
		__check_cores_waitlist_attached
		start_caches
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

# found cache and then core device:

for stop_caches in false true; do
	start_spdk
	start_caches
	add_cores
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_detached
	create_caches
	__check_caches_attached
	__check_cores_empty
	create_cores
	__check_cores_attached
	__check_cores_waitlist_empty
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# found core and then cache device:

for stop_caches in false true; do
	start_spdk
	start_caches
	add_cores
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_detached
	create_cores
	__check_cores_empty
	__check_cores_waitlist_attached
	create_caches
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

# found cache and then core device (inverse add order):

for stop_caches in false true; do
	start_spdk
	add_cores
	start_caches
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_detached
	create_caches
	__check_caches_attached
	__check_cores_empty
	create_cores
	__check_cores_attached
	__check_cores_waitlist_empty
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# found core and then cache device (inverse add order):

for stop_caches in false true; do
	start_spdk
	add_cores
	start_caches
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_detached
	create_cores
	__check_cores_empty
	__check_cores_waitlist_attached
	create_caches
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

# found core device (not yet added) on detached cache:

for stop_caches in false true; do
	start_spdk
	create_caches
	start_caches
	add_cores
	__check_caches_attached
	__check_cores_empty
	__check_cores_waitlist_detached
	detach_caches
	__check_caches_detached
	create_cores
	__check_caches_detached
	__check_cores_empty
	__check_cores_waitlist_attached
	attach_caches
	__check_caches_attached
	__check_cores_attached
	__check_cores_waitlist_empty
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done

# found core device (hotremoved) on detached cache:

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
	destroy_cores
	__check_cores_detached
	create_cores
	__check_caches_detached
	__check_cores_attached
	attach_caches
	__check_caches_attached
	__check_cores_attached
	if [ $stop_caches = true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
