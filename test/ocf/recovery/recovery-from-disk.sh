#!/usr/bin/env bash

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source $rootdir/test/ocf/common.sh
source $rootdir/scripts/common.sh
source $rootdir/test/common/autotest_common.sh
rpc_py=$rootdir/scripts/rpc.py


# Setup NVMe devices
$rootdir/scripts/setup.sh

# Create NVMe config
prepare_nvme_config

# Clear nvme device which we will use in test
clear_nvme

# Start SPDK app
start_spdk "$curdir/config"

# Create 2x256MiB partitions on NVMe device
create_partitions Nvme0n1 2 256

# Create CAS device
$rpc_py bdev_ocf_create cas_dev wb Nvme0n1p0 Nvme0n1p1 --create --force

# Save config
save_and_clean_bdev_config

# Stop SPDK app
stop_spdk

# Remove metadata files from RAM
rm -f /dev/shm/cas_dev*

# Check if recovery from disk works properly
start_spdk "$curdir/config"

$rpc_py bdev_ocf_get_bdevs | jq -r '.[] .started' | grep true
$rpc_py bdev_ocf_get_bdevs | jq -r '.[] .cache.attached' | grep true
$rpc_py bdev_ocf_get_bdevs | jq -r '.[] .core.attached' | grep true

# Stop SPDK app
stop_spdk
