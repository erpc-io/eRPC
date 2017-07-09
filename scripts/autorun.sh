num_nodes=60
down_node_list="43"	# List of down nodes

# Check that node-1 is not down. This node hosts the memcached registry.
if [[ $(echo $down_node_list | grep "\b1\b") ]]; then
    echo "autorun: Error. Node 1 in down list not allowed."
	exit
fi

# A function to echo in blue color
function blue() {
	es=`tput setaf 4`
	ee=`tput sgr0`
	echo "${es}$1${ee}"
}

#
# Creates a randomized list of physical nodes that:
# a. Includes all nodes from 1 through $num_nodes, except those in
#    $down_node_list
# b. The first node is "1"
#
function gen_random_nodes() {
	num_down_nodes=`echo $down_node_list | wc -w`
	num_up_nodes=`expr $num_nodes - $num_down_nodes`

	# Create a rand list of all nodes (both up nodes and down) excluding node-1
	node_list=`seq 2 $num_nodes | shuf`

	# Remove the down nodes from the list
	for down_node in $down_node_list; do
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

	# Save the mapping to a file to know which node hosts which HoTS machine-id
	# Done if the sourcing script sets overwrite_nodemap
	if [ $overwrite_nodemap -eq 1 ]; then
		rm -rf /tmp/autorun-nodemap
		touch /tmp/autorun-nodemap
		server_id=0
		for i in $node_list; do
			echo "machine-$server_id -> node $i" >> /tmp/autorun-nodemap
			server_id=`expr $server_id + 1`
		done
	fi

	echo $node_list
}

#
# Which cluster are we running on? CIB or CX3?
#
function get_cluster() {
	ip_addr=`ifconfig | grep -Eo 'inet (addr:)?([0-9]*\.){3}[0-9]*' | \
		grep -Eo '([0-9]*\.){3}[0-9]*' | grep -v '127.0.0.1' | head -1`

	# CIB nodes have IP addr of the form 10.113.1.*
	if [[ $ip_addr == "10.113.1"* ]]
	then
		echo "CIB"
	else
		# CX3 nodes have IP addr of the form 128.110.96.*
		if [[ $ip_addr == "128.110.96"* ]]
		then
			echo "CX3"
		else
			echo "autorun: Unidentified cluster. IP = $ip_addr"
			exit
		fi
	fi
}

# Set exported variables
autorun_app="tatp"	# The app to run
autorun_logdir="/tmp/$autorun_app/"	# Output directory at each machine
autorun_combined_logdir="/tmp/$autorun_app-combined" # Cumulative output dir
autorun_cluster=`get_cluster`

echo "autorun: Cluster identified = $autorun_cluster"

#
# Generate the node list consisting of IP addresses or DNS names
#
if [[ $autorun_cluster == "CIB" ]]
then
	autorun_hots="/home/anuj/hots"
	# The node list on CIB is static, so we always generate it
	node_list="47 48 49 50 51 52 46 65 66 91 92"
	for node in $node_list; do
		new_node=10.113.1.$node
		autorun_nodes=$autorun_nodes" "$new_node
	done
fi


if [[ $autorun_cluster == "CX3" ]]
then
	autorun_hots="/users/akalia/hots"
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

