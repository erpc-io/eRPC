#!/usr/bin/env bash
killall ib_write_bw

# Check arguments
if [ "$#" -ne 1 ]; then
  echo "Illegal args. Usage: client.sh <size>"
	exit
fi

ib_write_bw --gid-index=0 --port=3185 --ib-dev=mlx5_0 --ib-port=1 \
  --tx-depth=1 --report_gbits --run_infinitely --duration=1 \
  --size=$1 fawn-pluto0
