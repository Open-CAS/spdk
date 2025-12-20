#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../..")
source "$rootdir/test/ocf/common.sh"

# cores in waitlist:

for remove_cores in false true; do
	for create_cores in false true; do
		start_spdk
		create_cores
		add_cores
		__check_cores_waitlist_attached
		destroy_cores
		__check_cores_waitlist_detached
		if [ $create_cores == true ]; then
			create_cores
			__check_cores_waitlist_attached
		fi
		if [ $remove_cores == true ]; then
			remove_cores
			__check_cores_waitlist_empty
		fi
		stop_spdk
	done
done

# cores in waitlist (destroying only first):

for remove_cores in false true; do
	for create_cores in false true; do
		start_spdk
		create_cores
		add_cores
		__check_cores_waitlist_attached
		destroy_cores_only_first
		__check_cores_waitlist_detached_only_first
		if [ $create_cores == true ]; then
			create_cores_only_first
			__check_cores_waitlist_attached
		fi
		if [ $remove_cores == true ]; then
			remove_cores
			__check_cores_waitlist_empty
		fi
		stop_spdk
	done
done

# cores in waitlist (destroying all but first):

for remove_cores in false true; do
	for create_cores in false true; do
		start_spdk
		create_cores
		add_cores
		__check_cores_waitlist_attached
		destroy_cores_all_but_first
		__check_cores_waitlist_detached_all_but_first
		if [ $create_cores == true ]; then
			create_cores_all_but_first
			__check_cores_waitlist_attached
		fi
		if [ $remove_cores == true ]; then
			remove_cores
			__check_cores_waitlist_empty
		fi
		stop_spdk
	done
done

# caches only:

for create_caches in false true; do
	for stop_caches in false true; do
		start_spdk
		create_caches
		start_caches
		__check_caches_attached
		destroy_caches
		__check_caches_detached
		if [ $create_caches == true ]; then
			create_caches
			__check_caches_attached
		fi
		if [ $stop_caches == true ]; then
			stop_caches
			__check_caches_empty
		fi
		stop_spdk
	done
done

# caches only (destroying only first):

for create_caches in false true; do
	for stop_caches in false true; do
		start_spdk
		create_caches
		start_caches
		__check_caches_attached
		destroy_caches_only_first
		__check_caches_detached_only_first
		if [ $create_caches == true ]; then
			create_caches_only_first
			__check_caches_attached
		fi
		if [ $stop_caches == true ]; then
			stop_caches
			__check_caches_empty
		fi
		stop_spdk
	done
done

# caches only (destroying all but first):

for create_caches in false true; do
	for stop_caches in false true; do
		start_spdk
		create_caches
		start_caches
		__check_caches_attached
		destroy_caches_all_but_first
		__check_caches_detached_all_but_first
		if [ $create_caches == true ]; then
			create_caches_all_but_first
			__check_caches_attached
		fi
		if [ $stop_caches == true ]; then
			stop_caches
			__check_caches_empty
		fi
		stop_spdk
	done
done

# hotremove caches:

for remove_cores in false true; do
	for create_caches in false true; do
		for stop_caches in false true; do
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
			if [ $remove_cores == true ]; then
				remove_cores
				__check_caches_detached
				__check_cores_empty
			fi
			if [ $create_caches == true ]; then
				create_caches
				__check_caches_attached
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done

# hotremove caches (destroying only first):

for remove_cores in false true; do
	for create_caches in false true; do
		for stop_caches in false true; do
			start_spdk
			create_caches
			create_cores
			start_caches
			add_cores
			__check_caches_attached
			__check_cores_attached
			destroy_caches_only_first
			__check_caches_detached_only_first
			__check_cores_attached
			if [ $remove_cores == true ]; then
				remove_cores
				__check_caches_detached_only_first
				__check_cores_empty
			fi
			if [ $create_caches == true ]; then
				create_caches_only_first
				__check_caches_attached
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done

# hotremove caches (destroying all but first):

for remove_cores in false true; do
	for create_caches in false true; do
		for stop_caches in false true; do
			start_spdk
			create_caches
			create_cores
			start_caches
			add_cores
			__check_caches_attached
			__check_cores_attached
			destroy_caches_all_but_first
			__check_caches_detached_all_but_first
			__check_cores_attached
			if [ $remove_cores == true ]; then
				remove_cores
				__check_caches_detached_all_but_first
				__check_cores_empty
			fi
			if [ $create_caches == true ]; then
				create_caches_all_but_first
				__check_caches_attached
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done

# hotremove cores:

for create_cores in false true; do
	for remove_cores in false true; do
		for stop_caches in false true; do
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
			if [ $create_cores == true ]; then
				create_cores
				__check_caches_attached
				__check_cores_attached
			fi
			if [ $remove_cores == true ]; then
				remove_cores
				__check_caches_attached
				__check_cores_empty
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done

# hotremove cores (destroying only first):

for create_cores in false true; do
	for remove_cores in false true; do
		for stop_caches in false true; do
			start_spdk
			create_caches
			create_cores
			start_caches
			add_cores
			__check_caches_attached
			__check_cores_attached
			destroy_cores_only_first
			__check_caches_attached
			__check_cores_detached_only_first
			if [ $create_cores == true ]; then
				create_cores_only_first
				__check_caches_attached
				__check_cores_attached
			fi
			if [ $remove_cores == true ]; then
				remove_cores
				__check_caches_attached
				__check_cores_empty
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done

# hotremove cores (destroying all but first):

for create_cores in false true; do
	for remove_cores in false true; do
		for stop_caches in false true; do
			start_spdk
			create_caches
			create_cores
			start_caches
			add_cores
			__check_caches_attached
			__check_cores_attached
			destroy_cores_all_but_first
			__check_caches_attached
			__check_cores_detached_all_but_first
			if [ $create_cores == true ]; then
				create_cores_all_but_first
				__check_caches_attached
				__check_cores_attached
			fi
			if [ $remove_cores == true ]; then
				remove_cores
				__check_caches_attached
				__check_cores_empty
			fi
			if [ $stop_caches == true ]; then
				stop_caches
				__check_caches_empty
			fi
			stop_spdk
		done
	done
done

# hotremove caches and cores:

for create_caches_and_cores in false true; do
	for stop_caches in false true; do
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
		if [ $create_caches_and_cores == true ]; then
			create_caches
			__check_caches_attached
			create_cores
			__check_cores_attached
		fi
		if [ $stop_caches == true ]; then
			stop_caches
			__check_caches_empty
		fi
		stop_spdk
	done
done

# hotremove caches and cores, then manually remove cores:

for create_caches in false true; do
	for stop_caches in false true; do
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
		if [ $create_caches == true ]; then
			create_caches
			__check_caches_attached
			__check_cores_empty
		fi
		if [ $stop_caches == true ]; then
			stop_caches
			__check_caches_empty
		fi
		stop_spdk
	done
done

# hotremove caches and cores (inverse create after destroy order):

for stop_caches in false true; do
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
	if [ $stop_caches == true ]; then
		stop_caches
		__check_caches_empty
	fi
	stop_spdk
done
