#!/usr/bin/env bash

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../..)
source $rootdir/test/ocf/common.sh
source $rootdir/scripts/common.sh
source $rootdir/test/common/autotest_common.sh
rpc_py=$rootdir/scripts/rpc.py
bdevperf=$rootdir/test/bdev/bdevperf/bdevperf

# Setup NVMe devices
$rootdir/scripts/setup.sh
prepare_nvme_config

# Start SPDK app
start_spdk "$curdir/config"

# Create partitions for cache and core device
create_partitions Nvme0n1 2 100

# Create CAS device
$rpc_py bdev_ocf_create cas_dev wb Nvme0n1p0 Nvme0n1p1 --create --force

# Save current config and stop SPDK app
save_and_clean_bdev_config
echo '{ "subsystems": [ { "subsystem": "bdev", "config": ' $(cat "$curdir/config" | jq '.subsystems[] | select(.subsystem == "bdev") | .config[] | select(.method=="bdev_ocf_create").params |= . + { "force": true, "create": true}' | jq -s '.') ' } ] }' > "$curdir/config"
stop_spdk

for i in 4 8 16 32 64
do
    blocksize=$((1024*$i))

    # Run I/O in background
    $bdevperf --json "$curdir/config" -q 128 -o $blocksize -w write -t 60 -r /var/tmp/spdk.sock &
    bdevperf_pid=$!
    waitforlisten $bdevperf_pid
    sleep 1

    wr_pt_base=$(get_stat cas_dev requests wr_pt count)
    rd_pt_base=$(get_stat cas_dev requests rd_pt count)


    while $rpc_py bdev_ocf_get_bdevs | jq -r '.[] .name' | grep -qw cas_dev
    do
        # Check if there is no PT requests in statistics
        wr_pt=$(get_stat cas_dev requests wr_pt count)
        rd_pt=$(get_stat cas_dev requests rd_pt count)
        if [[ $(( $wr_pt - $wr_pt_base )) > 0 ]] || [[ $(( $rd_pt - $rd_pt_base )) > 0 ]]
        then
            echo >&2 "There are some PT requests. write: $(( $wr_pt - $wr_pt_base )); read: $(( $rd_pt - $rd_pt_base))"
            kill -9 $bdevperf_pid
            exit 1
        else
            echo "wr_pt: $(( $wr_pt - $wr_pt_base )); rd_pt: $(( $rd_pt - $rd_pt_base))"
        fi
        sleep 3
    done

    kill -9 $bdevperf_pid
done
