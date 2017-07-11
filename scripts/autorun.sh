#!/usr/bin/env bash
source $(dirname $0)/utils.sh

# Creates a randomized list of physical nodes that:
# a. Includes all nodes from 1 through $autorun_num_nodes, except those in
#    $autorun_down_node_list
# b. The first node is "1"
function gen_random_nodes() {
	num_down_nodes=`echo $autorun_down_node_list | wc -w`
	num_up_nodes=`expr $autorun_num_nodes - $num_down_nodes`

	# Create a rand list of all nodes (both up nodes and down) excluding node-1.
  # Just pipe to shuf for random order.
	node_list=`seq 2 $autorun_num_nodes`

	# Remove the down nodes from the list
	for down_node in $autorun_down_node_list; do
		node_list=`echo $node_list | sed "s/\b$down_node\b//g"`
	done

	# Append node 1 to the beginning of the list
	node_list=1" "$node_list

	# Sanity check
	node_list_size=`echo $node_list | wc -w`
	if [ "$node_list_size" -ne "$num_up_nodes" ]; then
		echo "autorun: Sanity check failed"
		exit
	fi

	# Save the mapping to a file to a tmp file so that the human user can figure
  # out the mapping of nodes to machine IDs.
	if [ $overwrite_nodemap -eq 1 ]; then
		rm -rf /tmp/autorun-nodemap
		touch /tmp/autorun-nodemap
		server_id=0
		for i in $node_list; do
			echo "machine-$server_id -> node $i" >> /tmp/autorun-debug-nodemap
			server_id=`expr $server_id + 1`
		done
	fi

	echo $node_list
}

# Which cluster are we running on?
function get_cluster() {
	ip_addr=`ifconfig | grep -Eo 'inet (addr:)?([0-9]*\.){3}[0-9]*' | \
		grep -Eo '([0-9]*\.){3}[0-9]*' | grep -v '127.0.0.1' | head -1`

  # CX3 nodes have IP addr of the form 128.110.96.*
  if [[ $ip_addr == "128.110.96"* ]]
  then
    echo "CX3"
  else
    blue "autorun: Unidentified cluster. IP = $ip_addr. Exiting."
    exit
  fi
}

# Get the config parameter specified in $1 for app = $autorun_app
function get_from_config() {
  config_file=$autorun_erpc_home/apps/$autorun_app/config
  if [ ! -f $config_file ]; then
    blue "autorun: App config file $config_file not found. Existing."
    exit
  fi

  config_param=`cat $config_file | grep $1 | cut -d ' ' -f 2`
  if [ -z $config_param ]; then
    blue "autorun: Parameter $1 absent in config file $config_file. Exiting."
    exit
  fi

  echo $config_param
}

# Variables set by the human user
autorun_erpc_home="/users/akalia/eRPC"
autorun_app="small_rpc_tput"	# The app to run
autorun_autorun_down_node_list=""	# List of down nodes

# Variables exported by this script
autorun_test_ms=`get_from_config "test_ms"`
autorun_num_nodes=`get_from_config "num_machines"`
autorun_cluster=`get_cluster`

blue "autorun: app = $autorun_app, test ms = $autorun_test_ms, num nodes = $autorun_num_nodes, cluster = $autorun_cluster"

# Check that node-1 is not down. This is just for convenience.
if [[ $(echo $autorun_down_node_list | grep "\b1\b") ]]; then
    echo "autorun: Error. Node 1 in down list not allowed."
	exit
fi

if [[ $autorun_cluster == "CX3" ]]
then
	# The node list on CX3 is dynamic, so generate nodes only if the sourcing
	# script set autorun_gen_nodes
	if [ $autorun_gen_nodes -eq 1 ]; then
		node_list=`gen_random_nodes`
		for node in $node_list; do
			new_node=akalianode-$node".RDMA.fawn.apt.emulab.net"
			autorun_nodes=$autorun_nodes" "$new_node
		done
	fi
fi

