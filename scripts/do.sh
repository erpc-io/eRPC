#!/usr/bin/env bash
# Run an eRPC app on this machine
source $(dirname $0)/utils.sh
drop_shm

app_name="small_rpc_tput"

# Install modded driver - this is not a requirement
(cd ~/libmlx4-1.2.1mlnx1/ && ./update_driver.sh)

sudo taskset -c 0 ./build/$app_name $(cat apps/$app_name/config) --machine_id $1
