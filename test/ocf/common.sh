#
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2025 Huawei Technologies
#  All rights reserved.
#

source "$rootdir/test/common/autotest_common.sh"

rpc_py="$rootdir/scripts/rpc.py"

start_spdk() {
	$SPDK_BIN_DIR/spdk_tgt "$@" &
	spdk_pid=$!
	trap 'killprocess $spdk_pid; exit 1' SIGINT SIGTERM EXIT
	waitforlisten $spdk_pid
}

stop_spdk() {
	trap - SIGINT SIGTERM EXIT
	killprocess $spdk_pid
}

# Convert an array of items to comma separated list of items.
array_to_comma_list() {
	# $1: name of array variable
	# stdout: string with comma separated elements

	IFS=","
	declare -n array="$1"
	echo "${array[*]}"
}
