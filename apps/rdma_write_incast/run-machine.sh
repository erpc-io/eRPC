#!/usr/bin/env bash
if [ "$#" -ne 1 ]; then
    blue "Illegal number of parameters"
	blue "Usage: ./run-machine.sh <machine_number>"
	exit
fi

killall ib_write_bw

# Install the original Mellanox OFED driver
(cd /proj/fawn/akalia/tars/MLNX_OFED_LINUX-3.4-2.0.0.0-ubuntu16.04-x86_64/src/MLNX_OFED_SRC-3.4-2.0.0.0/SOURCES/libmlx4-1.2.1mlnx1; ./update_driver.sh)

port=`expr $1 + 3185`
server_ip="akaliaNode-1.RDMA.fawn.apt.emulab.net"

ib_write_bw --size=65536 --run_infinitely --duration=1 --port=$port $server_ip
