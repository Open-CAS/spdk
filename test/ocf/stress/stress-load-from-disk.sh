#!/usr/bin/env bash

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source $rootdir/test/ocf/common.sh
source $rootdir/scripts/common.sh
source $rootdir/test/common/autotest_common.sh
rpc_py=$rootdir/scripts/rpc.py

iterations=50

# Setup NVMe devices
$rootdir/scripts/setup.sh

# Create NVMe config
prepare_nvme_config

# Clear NVMe device which we will use in test
clear_nvme

# Start SPDK app
start_spdk "$curdir/config"

# Create 2x256MiB partitions on NVMe device and save config
create_partitions Nvme0n1 2 256
save_and_clean_bdev_config

# Create CAS device
$rpc_py bdev_ocf_create cas_dev wb Nvme0n1p0 Nvme0n1p1 --create --force

# Check that CAS device was created properly
$rpc_py bdev_ocf_get_bdevs | jq -r '.[] .name' | grep -qw cas_dev

# Stop SPDK app
stop_spdk

# Remove shared memory files
rm -f /dev/shm/cas_dev*

# Test loop with loading CAS device
for i in $(eval echo "{1..$iterations}")
do 
    # Start SPDK app
    start_spdk "$curdir/config"

    # Load CAS device
    $rpc_py bdev_ocf_create cas_dev wb Nvme0n1p0 Nvme0n1p1

    # Check that CAS device was loaded properly
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .name' | grep -qw cas_dev
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .started' | grep true
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .cache.attached' | grep true
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .core.attached' | grep true

    # Stop SPDK app
    stop_spdk

    # Remove shared memory files
    rm -f /dev/shm/cas_dev*
done

# Cleanup
clear_nvme $bdf
remove_config
