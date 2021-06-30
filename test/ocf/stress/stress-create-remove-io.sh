#!/usr/bin/env bash

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source $rootdir/test/ocf/common.sh
source $rootdir/scripts/common.sh
source $rootdir/test/common/autotest_common.sh
rpc_py=$rootdir/scripts/rpc.py
bdevperf=$rootdir/test/bdev/bdevperf/bdevperf

iterations=55555che_modes=("wa" "wb" "wt" "pt" "wo" "wi")
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

    # Save current configuration and add force and create parameters
    $rpc_py save_config > "$curdir/config-cas"
    echo $(cat "$curdir/config-cas" | jq 'del(.subsystems[] | select(.subsystem != "bdev"))' | jq 'del(.subsystems[] | .config[] | select(.method != "bdev_split_create" and .method != "bdev_nvme_attach_controller" and .method != "bdev_ocf_create"))') > "$curdir/config-cas"
    echo '{ "subsystems": [ { "subsystem": "bdev", "config": ' $(cat "$curdir/config" | jq '.subsystems[] | select(.subsystem == "bdev") | .config[] | select(.method=="bdev_ocf_create").params |= . + { "force": true, "create": true}' | jq -s '.') ' } ] }' > "$curdir/config-cas"

    # Check that CAS device was created properly
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .name' | grep -qw cas_dev
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .started' | grep true
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .cache.attached' | grep true
    $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .core.attached' | grep true

    # Remove CAS device
    $rpc_py bdev_ocf_delete cas_dev

    # Check that CAS device was deleted properly
    ! $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .name' | grep -qw cas_dev

    # Stop SPDK app
    stop_spdk

    # Run I/O for 30s
    $bdevperf --json "$curdir/config-cas" -q 128 -o 4096 -w write -t 30
    
    # Clean NVMe
    clear_nvme
    
    # Start SPDK
    start_spdk "$curdir/config"
done

# Stop SPDK app and cleanup
stop_spdk

clear_nvme $bdf

remove_config
rm -f "$curdir/config-cas"
