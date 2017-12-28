#!/usr/bin/env bash
# Run an eRPC app on this machine
source $(dirname $0)/utils.sh
check_env "autorun_app"

# Check the executable
if [ ! -f build/$autorun_app ]; then
  blue "$autorun_app executable not found in build/"
fi

chmod +x build/$autorun_app # Fix permissions messed up by lsyncd

export MLX4_SINGLE_THREADED=1
export MLX5_SINGLE_THREADED=1
export MLX5_SHUT_UP_BF=1
export MLX_QP_ALLOC_TYPE="HUGE"
export MLX_CQ_ALLOC_TYPE="HUGE"

# Install modded driver - this is not a requirement
if [ "$autorun_app" != "consensus" ]; then
  blue "Installing modded driver"
  (cd ~/libmlx4-1.2.1mlnx1/ && ./update_driver.sh)
else
  # The consensus app latency improves with inline size = 120 bytes. However,
  # the modded driver only supports inline size = 60 bytes.
  blue "Installing original driver for consensus app"
  ~/install-original-driver.sh
fi

# Check arguments
if [ "$#" -gt 3 ] || [ "$#" -lt 2 ]; then
  blue "Illegal args. Usage: do.sh [process_id] [NUMA node] <gdb>"
	exit
fi

epid=$1
numa_node=$2

# Non-GDB mode
if [ "$#" -eq 2 ]; then
  blue "do.sh: Launching process $epid on NUMA node $numa_node"

  sudo -E numactl --cpunodebind=$numa_node --membind=$numa_node \
    ./build/$autorun_app $(cat apps/$autorun_app/config) \
    --process_id $epid --numa_node $numa_node
fi

# GDB mode
if [ "$#" -eq 3 ]; then
  blue "do.sh: Launching process $epid with GDB"
  sudo gdb -ex run --args \
    ./build/$autorun_app $(cat apps/$autorun_app/config) \
    --process_id $epid --numa_node $numa_node
fi
