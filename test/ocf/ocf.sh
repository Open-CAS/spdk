#!/usr/bin/env bash

testdir=$(readlink -f $(dirname $0))
rootdir=$(readlink -f $testdir/../..)

source $rootdir/test/common/autotest_common.sh


run_test "ocf_stress_create_remove" "$testdir/stress/stress-create-remove.sh"
run_test "ocf_stress_create_remove_io" "$testdir/stress/stress-create-remove-io.sh"
run_test "ocf_stress_load_from_disk" "$testdir/stress/stress-load-from-disk.sh"
run_test "ocf_stress_load_from_disk_io" "$testdir/stress/stress-load-from-disk-io.sh"
run_test "ocf_stress_load_from_ram_io" "$testdir/stress/stress-load-from-ram-io.sh"
run_test "ocf_stress_get_stats" "$testdir/stress/stress-get-stats.sh"

run_test "ocf_fio_modes" "$testdir/integrity/fio-modes.sh"
run_test "ocf_bdevperf_iotypes" "$testdir/integrity/bdevperf-iotypes.sh"
run_test "ocf_stats" "$testdir/integrity/stats.sh"

run_test "ocf_create_destruct" "$testdir/management/create-destruct.sh"
run_test "ocf_multicore" "$testdir/management/multicore.sh"
run_test "ocf_persistent_metadata" "$testdir/management/persistent-metadata.sh"
run_test "ocf_remove" "$testdir/management/remove.sh"
run_test "ocf_configuration_change" "$testdir/management/configuration-change.sh"
