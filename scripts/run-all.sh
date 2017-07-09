#!/bin/bash

# Request autorun to generate a new randomized list of nodes.
# Save the random order to the nodemap.
autorun_gen_nodes=1
overwrite_nodemap=1
source $(dirname $0)/autorun.sh

# Print the generated nodes (one per line)
echo "run-all: autorun_nodes ="
echo $autorun_nodes | tr ' ' '\n'

rm -rf $autorun_logdir
mkdir $autorun_logdir

server_id=0
for node in $autorun_nodes; do
	echo "Starting machine-$server_id on $node"
	ssh -oStrictHostKeyChecking=no $node \
		"cd $autorun_hots/app/$autorun_app/; ./run-remote.sh $server_id" &

	server_id=`expr $server_id + 1`

	if [ "$server_id" -eq 1 ]; then
		echo "Giving 2 seconds for memcached on node 1 to start"
		sleep 2
	fi
done
