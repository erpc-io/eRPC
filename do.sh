app_name="small_rpc_tput"

# Drop SHM
for i in $(ipcs -m | awk '{ print $1; }'); do
    if [[ $i =~ 0x.* ]]; then
        sudo ipcrm -M $i 2>/dev/null
    fi
done

# Install modded driver - this is not strictly needed
(cd ~/libmlx4-1.2.1mlnx1/ && ./update_driver.sh)


sudo ./build/$app_name $(cat apps/$app_name/config) --machine_id $1
