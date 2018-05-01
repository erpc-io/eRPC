#!/usr/bin/env bash
killall ib_write_bw

# Check arguments
if [ "$#" -ne 1 ]; then
  echo "Illegal args. Usage: client.sh <size in KB>"
	exit
fi

size=`expr 1024 \* $1`

# We need tx-depth=1 to have only one WRITE in flight
ib_write_bw --gid-index=0 --port=3185 --ib-dev=mlx5_3 --ib-port=1 \
  --tx-depth=1 --report_gbits --run_infinitely --duration=1 \
  --size=$size akalianode-1.rdma.ron-pg0.utah.cloudlab.us
