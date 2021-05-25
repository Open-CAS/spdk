#!/usr/bin/env bash

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source $rootdir/test/ocf/common.sh
source $rootdir/scripts/common.sh
source $rootdir/test/common/autotest_common.sh
rpc_py=$rootdir/scripts/rpc.py

iterations=50
cache_modes=("wa" "wb" "wt" "pt" "wo" "wi")
RANDOM=$$$(date +%s)

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

# Test loop with creating and deleting CAS device
for i in $(eval echo "{1..$iterations}")
do
    # Create CAS device
    random_cache_mode=${cache_modes[$RANDOM % ${#cache_modes[@]}]}
    $rpc_py bdev_ocf_create cas_dev $random_cache_mode Nvme0n1p0 Nvme0n1p1 --create --force

    # Check that CAS device was created properly
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .name' | grep -qw cas_dev

    # Remove CAS device
    $rpc_py bdev_ocf_delete cas_dev

    # Check that CAS device was deleted properly
    ! $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .name' | grep -qw cas_dev
done

# Stop SPDK app and cleanup
stop_spdk

clear_nvme $bdf

remove_config
