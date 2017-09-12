#!/bin/bash
source $(dirname $0)/../../util/helpers.sh

echo "Removing SHM keys"
drop_shm

sudo numactl --membind=0 ./main
