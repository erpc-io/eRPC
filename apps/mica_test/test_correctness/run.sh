#!/bin/bash
source $(dirname $0)/../../util/helpers.sh

echo "Removing SHM keys"
drop_shm

sudo numactl --physcpubind=0 --membind=0 ./test_table
