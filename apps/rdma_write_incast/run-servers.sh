#!/usr/bin/env bash
source $(dirname $0)/../../scripts/utils.sh
killall ib_write_bw

num_processes=15
port=3185

# Install the original Mellanox OFED driver
(cd /proj/fawn/akalia/tars/MLNX_OFED_LINUX-3.4-2.0.0.0-ubuntu16.04-x86_64/src/MLNX_OFED_SRC-3.4-2.0.0.0/SOURCES/libmlx4-1.2.1mlnx1; ./update_driver.sh)

for i in `seq 1 $num_processes`; do
  echo "Running on port = $port"
  ib_write_bw --port=$port &
  ((port+=1))
done

wait
