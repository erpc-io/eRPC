#!/usr/bin/env bash
source $(dirname $0)/opcode.sh
killall $opcode
numactl --physcpubind=0 --membind=0 $opcode --ib-dev=mlx5_1 --ib-port=1 --size=8388608
