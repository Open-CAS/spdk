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

# Create 3x256MiB partitions on NVMe device
create_partitions Nvme0n1 3 256 

# Create CAS device
$rpc_py bdev_ocf_create cas_dev wb Nvme0n1p0 Nvme0n1p1 --create --force

# Save config
save_and_clean_bdev_config

# Stop SPDK app
stop_spdk

# Copy original config file to temporary file
cp "$curdir/config" /tmp/config

for i in 1 2
do
    # Clear NVMe devices
    clear_nvme

    # Change cache device in config
    sed -i "s/Nvme0n1p$i/Nvme0n1p3/g" "$curdir/config"

    # Start SPDK app with config and check if CAS did not start successfully
    start_spdk "$curdir/config"

    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .started' | grep false
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .cache.attached' | grep false
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .core.attached' | grep false

    # Stop SPDK app
    stop_spdk

    # Restore original config
    cp /tmp/config "$curdir/config"
done

