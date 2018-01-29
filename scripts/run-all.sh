#!/usr/bin/env bash
source $(dirname $0)/utils.sh
source $(dirname $0)/autorun_parse.sh

# Print the generated processes
echo "autorun processes: ["
for i in `seq 1 $autorun_num_processes`; do
  echo ${autorun_name_list[$i]} ${autorun_udp_port_list[$i]} ${autorun_numa_list[$i]}
done
echo "]"

echo "eRPC home = $autorun_erpc_home"

# Check autorun_app
assert_file_exists $autorun_erpc_home/scripts/autorun_app_file
export autorun_app=`cat $autorun_erpc_home/scripts/autorun_app_file`

# Check that the app has been built
assert_file_exists $autorun_erpc_home/build/$autorun_app

# Check the app config file
app_config=$autorun_erpc_home/apps/$autorun_app/config
assert_file_exists $app_config
app_args=`cat $app_config | tr '\n' ' '`

for ((i = 0; i < $autorun_num_processes; i++)); do
  name=${autorun_name_list[$i]}
  # We don't need the UDP port - the C++ code figures it out
  numa_node=${autorun_numa_list[$i]}

  out_file="$autorun_out_prefix-$i"
  err_file="$autorun_err_prefix-$i"

  echo "run-all: Starting process-$i on $name (port $udp_port, NUMA $numa_node)"
	ssh -oStrictHostKeyChecking=no $name "\
    /users/akalia/libmlx4-1.2.1mlnx1/update_driver.sh; \
    cd $autorun_erpc_home; \
    source scripts/utils.sh; \
    drop_shm; \
    sudo nohup numactl --physcpubind $numa_node --membind $numa_node \
    ./build/$autorun_app $app_args --process_id $i --numa_node $numa_node \
    > $out_file 2> $err_file < /dev/null &" &
done
wait

# Allow the app to run
app_sec=`echo "scale=1; $autorun_test_ms / 1000" | bc -l`
sleep_sec=`echo "scale=1; $app_sec + 10" | bc -l`
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
