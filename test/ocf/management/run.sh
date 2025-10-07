#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../..")

source "$rootdir/test/common/autotest_common.sh"

run_test "start-stop" "$curdir/start-stop/run.sh"
run_test "info_dump" "$curdir/info_dump.sh"
run_test "detach-attach" "$curdir/detach-attach.sh"
run_test "hotremove" "$curdir/hotremove.sh"
run_test "config_change" "$curdir/config_change.sh"
