#!/usr/bin/env bash

#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

curdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
rootdir=$(readlink -f $curdir/../../../..)

source $rootdir/test/common/autotest_common.sh

run_test "basic" "$curdir/basic.sh"
run_test "incomplete" "$curdir/incomplete.sh"
run_test "examine" "$curdir/examine.sh"
run_test "load" "$curdir/load.sh"
