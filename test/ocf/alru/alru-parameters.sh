#!/usr/bin/env bash

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source $rootdir/test/ocf/common.sh
source $rootdir/scripts/common.sh
source $rootdir/test/common/autotest_common.sh
rpc_py=$rootdir/scripts/rpc.py


activity_threshold=10
flush_max_buffers=7000
staleness_time=2
wake_up_time=15

# Setup NVMe devices
$rootdir/scripts/setup.sh

# Create NVMe config
prepare_nvme_config

# Start SPDK app
start_spdk "$curdir/config"

# Create partitions for cache and core device
create_partitions Nvme0n1 1 100
create_partitions Nvme1n1 1 150

# Create CAS device
$rpc_py bdev_ocf_create cas_dev wb Nvme0n1p0 Nvme1n1p0 --create --force

# Set cleaning policy to ALRU
$rpc_py bdev_ocf_set_cleaning_alru cas_dev

# Set params
$rpc_py bdev_ocf_set_cleaning_alru cas_dev -w $wake_up_time -b $flush_max_buffers \
-s $staleness_time -t $activity_threshold

# Check each cleaning param value
$rpc_py bdev_ocf_get_bdevs | jq -r '.[] .cache.cleaning.wake_up' | grep $wake_up_time
$rpc_py bdev_ocf_get_bdevs | jq -r '.[] .cache.cleaning.flush_max_buffers' | grep $flush_max_buffers
$rpc_py bdev_ocf_get_bdevs | jq -r '.[] .cache.cleaning.staleness_time' | grep $staleness_time
$rpc_py bdev_ocf_get_bdevs | jq -r '.[] .cache.cleaning.activity_threshold' | grep \
$activity_threshold

# Stop SPDK app
stop_spdk
