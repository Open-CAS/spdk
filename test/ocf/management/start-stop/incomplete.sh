#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../../..")
source "$rootdir/test/ocf/common.sh"

# incomplete caches:

for stop_caches in false true; do
	start_spdk
	start_caches
	__check_caches_detached
	__check_cores_waitlist_empty
	__check_cores_empty
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_empty
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# incomplete cores:

for remove_cores in false true; do
	start_spdk
	add_cores
	__check_caches_empty
	__check_cores_waitlist_detached
	if [ $remove_cores == true ]; then
		remove_cores
		__check_caches_empty
		__check_cores_waitlist_empty
	fi
	stop_spdk
done

# incomplete caches and incomplete cores:

for inverse in false true; do
	for remove_cores in false true; do
		for stop_caches in false true; do
			start_spdk
			if [ $inverse == false ]; then
				start_caches
				add_cores
			else
				add_cores
				start_caches
			fi
			__check_caches_detached
			__check_cores_waitlist_detached
			__check_cores_empty
			if [ $remove_cores == true ]; then
				remove_cores
				__check_caches_detached
				__check_cores_waitlist_empty
				__check_cores_empty
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
				if [ $remove_cores == true ]; then
					__check_cores_waitlist_empty
				else
					__check_cores_waitlist_detached
				fi
			fi
			stop_spdk
		done
	done
done

# complete caches and incomplete cores:

for inverse in false true; do
	for remove_cores in false true; do
		for stop_caches in false true; do
			start_spdk
			create_caches
			if [ $inverse == false ]; then
				start_caches
				add_cores
			else
				add_cores
				start_caches
			fi
			__check_caches_base_claimed
			__check_caches_attached
			__check_cores_waitlist_detached
			__check_cores_empty
			if [ $remove_cores == true ]; then
				remove_cores
				__check_caches_base_claimed
				__check_caches_attached
				__check_cores_waitlist_empty
				__check_cores_empty
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_base_not_claimed
				__check_caches_empty
				if [ $remove_cores == true ]; then
					__check_cores_waitlist_empty
				else
					__check_cores_waitlist_detached
				fi
			fi
			stop_spdk
		done
	done
done

# incomplete caches and complete cores:

for inverse in false true; do
	for remove_cores in false true; do
		for stop_caches in false true; do
			start_spdk
			create_cores
			if [ $inverse == false ]; then
				start_caches
				add_cores
			else
				add_cores
				start_caches
			fi
			__check_caches_detached
			__check_cores_base_claimed
			__check_cores_waitlist_attached
			__check_cores_empty
			if [ $remove_cores == true ]; then
				remove_cores
				__check_caches_detached
				__check_cores_base_not_claimed
				__check_cores_waitlist_empty
				__check_cores_empty
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
				if [ $remove_cores == true ]; then
					__check_cores_base_not_claimed
					__check_cores_waitlist_empty
				else
					__check_cores_base_claimed
					__check_cores_waitlist_attached
				fi
			fi
			stop_spdk
		done
	done
done
