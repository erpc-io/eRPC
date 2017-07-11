#!/usr/bin/env bash
source $(dirname $0)/utils.sh

# Request autorun to generate a new randomized list of nodes. This saves the
# node-to-machine-ID mapping to a nodemap file.
autorun_gen_nodes=1
overwrite_nodemap=1
source $(dirname $0)/autorun.sh

# Print the generated nodes (one per line)
blue "run-all: app = $autorun_app, nodes = [["
echo $autorun_nodes | tr ' ' '\n'
blue "]]"

# Check that the app has been built
assert_file_exists $autorun_erpc_home/build/$autorun_app

# Check the app config file
app_config=$autorun_erpc_home/apps/$autorun_app/config
assert_file_exists $app_config
app_args=`cat $app_config | tr '\n' ' '`

server_id=0
for node in $autorun_nodes; do
	echo "run-all: Starting machine-$server_id on $node"
	ssh -oStrictHostKeyChecking=no $node "\
    /users/akalia/libmlx4-1.2.1mlnx1/update_driver.sh; \
    cd $autorun_erpc_home; \
    source scripts/utils.sh; \
    drop_shm; \
    sudo nohup ./build/$autorun_app $app_args --machine_id $server_id \
    > /dev/null 2> /dev/null < /dev/null &" &

	server_id=`expr $server_id + 1`
done
wait

# Allow the app to run
app_sec=`echo "scale=1; $autorun_test_ms / 1000" | bc -l`
sleep_sec=`echo "scale=1; $app_sec + 10" | bc -l`
blue "run-all: Sleeping for $sleep_sec seconds. App will run for $app_sec seconds."
sleep $sleep_sec

# Print nodes on which the app is still running
blue "run-all: Printing nodes on which $autorun_app is still running..."
for node in $autorun_nodes; do
  (
	ret=`ssh -oStrictHostKeyChecking=no $node "pgrep -x $autorun_app"`
  if [ -n "$ret" ]; then
    echo "run-all: $autorun_app still running on $node"
  fi
  )&
done
wait
blue "...Done"
