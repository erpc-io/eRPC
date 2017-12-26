#!/usr/bin/env bash
# Run an eRPC app on this machine
source $(dirname $0)/utils.sh
check_env "autorun_app"
dual_process=1

drop_shm

# Check the executable
if [ ! -f build/$autorun_app ]; then
  blue "$autorun_app executable not found in build/"
fi

chmod +x build/$autorun_app # Fix permissions messed up by lsyncd

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
if [ "$#" -gt 2 ] || [ "$#" -eq 0 ]; then
  blue "Illegal args. Usage: do.sh <process_id>, or do.sh gdb <process_id>"
	exit
fi

# Non-GDB mode
if [ "$#" -eq 1 ]; then
  if [ "$dual_process" -eq 0 ]; then
    # Compute eRPC process IDs
    epid_0=`expr 2 \* $1`
    epid_1=`expr 2 \* $1 + 1`
    blue "do.sh: Launching process $epid_0 (NUMA 0) and $epid_1 (NUMA 1)"

    sudo numactl --cpunodebind=0 --membind=0 ./build/$autorun_app \
      $(cat apps/$autorun_app/config) --process_id $epid_0 --numa_node 0

    sudo numactl --cpunodebind=1 --membind=1 ./build/$autorun_app \
      $(cat apps/$autorun_app/config) --process_id $epid_1 --numa_node 1
  else
    epid_0=$1
    blue "do.sh: Launching process $epid_0 (NUMA 0)"

    sudo numactl --cpunodebind=0 --membind=0 ./build/$autorun_app \
      $(cat apps/$autorun_app/config) --process_id $epid_0 --numa_node 0
  fi
fi

# GDB mode
if [ "$#" -eq 2 ]; then
  if [ "$dual_process" -eq 1 ]; then
    blue "do.sh: Cannot run GDB in dual-process mode. Exiting. "
    exit
  fi

  blue "do.sh: machine ID = $1. Launching GDB."
  sudo gdb -ex run --args ./build/$autorun_app $(cat apps/$autorun_app/config) --process_id $2
fi
