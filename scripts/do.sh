#!/usr/bin/env bash
# Run an eRPC app on this machine
source $(dirname $0)/utils.sh
drop_shm
check_env "autorun_app"

# Install modded driver - this is not a requirement
(cd ~/libmlx4-1.2.1mlnx1/ && ./update_driver.sh)

sudo taskset -c 0 ./build/$autorun_app $(cat apps/$autorun_app/config) --machine_id $1
