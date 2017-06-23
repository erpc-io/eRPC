for i in $(ipcs -m | awk '{ print $1; }'); do
    if [[ $i =~ 0x.* ]]; then
        sudo ipcrm -M $i 2>/dev/null
    fi
done

(cd ~/libmlx4-1.2.1mlnx1/ && ./update_driver.sh)
sudo ./build/small_rpc_tput $(cat apps/small_rpc_tput/config) --machine_id $1
