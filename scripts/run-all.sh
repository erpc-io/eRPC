#!/usr/bin/env bash
source $(dirname $0)/utils.sh
source $(dirname $0)/autorun_parse.sh

# Print the generated processes
echo "autorun processes: ["
for ((i = 0; i < $autorun_num_processes; i++)); do
  echo ${autorun_name_list[$i]} ${autorun_udp_port_list[$i]} ${autorun_numa_list[$i]}
done
echo "]"

echo "eRPC home = $autorun_erpc_home"

# Check the app config file
app_config=$autorun_erpc_home/apps/$autorun_app/config
assert_file_exists $app_config
app_args=`cat $app_config | tr '\n' ' '`

# Install modded drivers (or not)
# modded_drivers_cmd="\
#   sudo ./drivers/4.4/libmlx5-41mlnx1/update-driver.sh; \
#   sudo ./drivers/4.2/libmlx4-41mlnx1/update-driver.sh;"
modded_drivers_cmd=":"

for ((i = 0; i < $autorun_num_processes; i++)); do
  name=${autorun_name_list[$i]}
  # We don't need the UDP port - the C++ code figures it out
  numa_node=${autorun_numa_list[$i]}

  out_file="$autorun_out_prefix-$i"
  err_file="$autorun_err_prefix-$i"

  echo "run-all: Starting process-$i on $name, NUMA $numa_node"
  ssh -oStrictHostKeyChecking=no $name "\
    sudo rm -rf /tmp/${autorun_app}*; \
    cd $autorun_erpc_home; \
    source scripts/utils.sh; \
    export autorun_app=$autorun_app; \
    drop_shm; \
    $modded_drivers_cmd; \
    sudo -E env LD_LIBRARY_PATH=$LD_LIBRARY_PATH \
    nohup numactl --physcpubind $numa_node --membind $numa_node \
    ./build/$autorun_app $app_args --process_id $i --numa_node $numa_node \
    > $out_file 2> $err_file < /dev/null &" &
done
wait

# Allow the app to run
app_sec=`echo "scale=1; $autorun_test_ms / 1000" | bc -l`
sleep_sec=`echo "scale=1; $app_sec + 5" | bc -l`
blue "run-all: Sleeping for $sleep_sec seconds. App will run for $app_sec seconds."
sleep $sleep_sec

# Print processes that are still running
blue "run-all: Printing $autorun_app processes that are still running..."
for ((i = 0; i < $autorun_num_processes; i++)); do
  (
	ret=`ssh -oStrictHostKeyChecking=no ${autorun_name_list[$i]} \
    "pgrep -x $autorun_app"`

  if [ -n "$ret" ]; then
    echo "run-all: $autorun_app still running on $autorun_name_list[$i]"
  fi
  )&
done
wait
blue "...Done"

# Retrieve each process's output and print the result. This is optional.
#$(dirname $0)/proc-out.sh
