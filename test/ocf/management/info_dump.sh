#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../..")
source "$rootdir/test/ocf/common.sh"

start_spdk
create_caches
$rpc_py bdev_ocf_start_cache Ocf_cache1 Cache_dev1 --no-load
$rpc_py bdev_ocf_start_cache Ocf_cache2 Cache_dev2 --no-load
$rpc_py bdev_ocf_start_cache Ocf_cache3 Cache_dev3 --no-load --cache-mode pt --cache-line-size 64
create_cores
add_cores

# First cache detached.
$rpc_py bdev_malloc_delete Cache_dev1
# First core in each cache detached.
for i in {1..3}; do
	$rpc_py bdev_malloc_delete Core_dev$i-1
done
# First core in wait list detached.
for i in {2..3}; do
	$rpc_py bdev_malloc_create -b Core_dev_waitlist$i 200 512
done
for i in {1..3}; do
	$rpc_py bdev_ocf_add_core Ocf_core_waitlist$i Core_dev_waitlist$i Ocf_cache_none
done
$rpc_py bdev_ocf_set_cachemode Ocf_cache2 wb

$rpc_py bdev_ocf_get_bdevs | jq -e .

# Test OCF statistics:
$rpc_py bdev_ocf_get_stats Ocf_cache1 | jq -e \
	'([.requests[], .blocks[], .errors[] | .count] | any(. != 0)) and
	([.requests[], .blocks[], .errors[] | .percentage] | any(. != "0.0"))'
$rpc_py bdev_ocf_reset_stats Ocf_cache1
$rpc_py bdev_ocf_get_stats Ocf_cache1 | jq -e \
	'.requests[], .blocks[], .errors[] | .count == 0 and .percentage == "0.0"'

# Test OCF get bdevs:
diff <($rpc_py bdev_ocf_get_bdevs | jq -e .) <(jq -e . "$curdir/info_dump_get_bdevs.json")

# Test general get bdevs driver specific info:
diff <($rpc_py bdev_get_bdevs | jq -e '.[].driver_specific.ocf | select(. != null)') \
	<(jq -e . "$curdir/info_dump_driver_specific.json")

# Test current config dump:
diff <($rpc_py save_subsystem_config -n bdev | jq -e '.config[] | select(.method|test("bdev_ocf_"))') \
	<(jq -e . "$curdir/info_dump_save_config.json")

stop_spdk
