#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f "$curdir/../../..")

source "$rootdir/test/common/autotest_common.sh"

run_test "io_stats" "$curdir/io_stats.sh"
run_test "mngt_during_io" "$curdir/mngt_during_io.sh"
run_test "flush" "$curdir/flush.sh"
run_test "stress" "$curdir/stress.sh"
