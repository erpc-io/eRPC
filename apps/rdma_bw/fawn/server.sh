#!/usr/bin/env bash
source $(dirname $0)/opcode.sh
killall $opcode
$opcode --gid-index=0 --port=3185 --ib-dev=mlx5_0 --ib-port=1 --size=8388608
