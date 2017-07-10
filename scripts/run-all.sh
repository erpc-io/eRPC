#!/usr/bin/env bash
source $(dirname $0)/utils.sh

# Request autorun to generate a new randomized list of nodes. This saves the
# node-to-machine-ID mapping to a nodemap file.
autorun_gen_nodes=1
overwrite_nodemap=1
source $(dirname $0)/autorun.sh

# Print the generated nodes (one per line)
echo "run-all: app = $autorun_app, nodes = "
echo $autorun_nodes | tr ' ' '\n'

# Check that the app has been built
assert_file_exists $autorun_erpc_home/build/$autorun_app

# Check the app config file
app_config=$autorun_erpc_home/apps/$autorun_app/config
assert_file_exists $app_config
app_args=`cat $app_config | tr '\n' ' '`

server_id=0
for node in $autorun_nodes; do
	echo "Starting machine-$server_id on $node"
	ssh -oStrictHostKeyChecking=no $node "\
    cd $autorun_erpc_home; \
    source scripts/utils.sh; \
    drop_shm; \
    sudo nohup ./build/$autorun_app $app_args --machine_id $server_id \
    > /dev/null 2> /dev/null < /dev/null &"

	server_id=`expr $server_id + 1`
done
