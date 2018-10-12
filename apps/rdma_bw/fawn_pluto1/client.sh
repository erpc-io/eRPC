#!/usr/bin/env bash
source $(dirname $0)/opcode.sh

# Check arguments
if [ "$#" -ne 1 ]; then
  echo "Illegal args. Usage: client.sh <size in KB>"
	exit
fi

size=512

# We need tx-depth=1 to have only one WRITE in flight
numactl --physcpubind=0 --membind=0 $opcode --ib-dev=mlx5_3 --ib-port=1 \
  --tx-depth=1 --report_gbits --run_infinitely --duration=1 \
  --size=$size fawn-pluto1
