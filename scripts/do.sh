#!/usr/bin/env bash
# Run an eRPC app

app_name="large_rpc_tput"

# Drop SHM
for i in $(ipcs -m | awk '{ print $1; }'); do
    if [[ $i =~ 0x.* ]]; then
        sudo ipcrm -M $i 2>/dev/null
    fi
done

# Install modded driver - this is not a requirement
(cd ~/libmlx4-1.2.1mlnx1/ && ./update_driver.sh)

sudo taskset -c 0 ./build/$app_name $(cat apps/$app_name/config) --machine_id $1
