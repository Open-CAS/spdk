#!/usr/bin/env bash

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source $rootdir/test/ocf/common.sh
source $rootdir/scripts/common.sh
source $rootdir/test/common/autotest_common.sh
rpc_py=$rootdir/scripts/rpc.py
bdevperf=$rootdir/test/bdev/bdevperf/bdevperf
iterations=50

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
$rpc_py bdev_ocf_create cas_dev wt Nvme0n1p0 Nvme0n1p1 --create --force

# Check that CAS device was created properly
$rpc_py bdev_ocf_get_bdevs | jq -r '.[] .name' | grep -qw cas_dev

save_and_clean_bdev_config
stop_spdk

# Start I/O in background
$bdevperf --json "$curdir/config" -q 128 -o 4096 -w write -t 360 -r /var/tmp/spdk.sock &
bdev_perf_pid=$!
waitforlisten $bdev_perf_pid
sleep 1

# Test loop with getting CAS device statistics
for i in $(eval echo "{1..$iterations}")
do 
    get_stat_json cas_dev | jq
    sleep 1
done

# Cleanup
kill -9 $bdev_perf_pid
wait $bdev_perf_pid || true
clear_nvme $bdf
remove_config
