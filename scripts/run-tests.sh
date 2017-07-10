#!/usr/bin/env bash
# A convenience wrapper around ctest
source $(dirname $0)/utils.sh
drop_shm
sudo ctest
