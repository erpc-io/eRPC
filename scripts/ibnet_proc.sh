#!/usr/bin/env bash
source $(dirname $0)/utils.sh

#
# Usage: ./ibnet_proc.sh < ibnet_out
# ibnet_out should be generated using `sudo ibnetdiscover`. Tested version o
# ibnetdiscover is 1.6.6.MLNX20160814.999c7b2
#
# 1. Prints the nodes connected to each switch using an ibnetdiscover file
# 2. For a given subset set of nodes (see $apt_nodes below), prints the number
#    of nodes connected to each switch
#

nodename_prefix="akalianode-" # The prefix of node names to find

# Save the node-to-switch mapping to a unique file based on the reboot name
reboot_uniq_str=`uptime -s | tr ' ' '-'`
node_to_switch_map_file=`echo /tmp/node_to_switch_map_$reboot_uniq_str`
rm -f $node_to_switch_map_file

#
# Part 1: Print nodes connected to each switch
#
switch_i=0	# ID of the current switch that we are parsing
total_nodes=0	# Total nodes detected
switch_map[0]="" # ${switch_map[i]$ contains the list of nodes connected to it

while read line
do
	# When we detect a new line with "switchguid", we're starting a new switch
	if [[ $(echo $line | grep switchguid) ]]; then
		switch_i=`expr $switch_i + 1`
		echo "Processing switch $switch_i"
	fi

	# A line with both nodename_prefix and "lid", uniquely represents a connected
  # node. (There are two lines in the ibnetdiscover file containing any given
  # node name.)
	if [[ $(echo $line | grep $nodename_prefix | grep lid) ]]; then
		# Grep out the node and append it to this switch's list of nodes
		node=`echo $line | grep -o "$nodename_prefix[0-9]\+"`
		switch_map[$switch_i]=$node" "${switch_map[$switch_i]}
    echo "$node $switch_i" >> $node_to_switch_map_file
    ((total_nodes+=1))
	fi

	# No need to process per-node info. The per-node information blocks are are
	# at the end of the ibnet_out file, and each block contains "caguid".
	if [[ $(echo $line | grep caguid) ]]; then
		break
	fi
done

# Sort the node-to-switch map file by node name in place. This assumes that
# node names are of the form <prefix>-<number>, e.g., akalianode-1
sort -t '-' -k 2 -g $node_to_switch_map_file -o $node_to_switch_map_file

echo ""
blue "Nodes under each switch:"
# Actually print the nodes
for i in `seq 1 10`; do
	num_nodes=`echo ${switch_map[$i]} | wc -w`

  # This assumes that node names are of the form <prefix>-<number>, e.g., akalianode-1
	sorted_nodes=`echo ${switch_map[$i]} | xargs -n 1 | sort -t '-' -k 2 -g | tr '\n' ' '`
	echo "Switch $i ($num_nodes nodes): $sorted_nodes"
done

# To check that we detected all nodes, print the number of nodes parsed
echo "Total nodes under all switches = $total_nodes"
echo ""

#
# Part 2: For a subset of nodes in apt_nodes, print the number of nodes
#         connected to each switch. This is useful to check if a particular
#         switch is overloaded in this node selection. apt_nodes below can
#         be replaced with any subset of {1, ..., $total_nodes}
#
apt_nodes=`seq -s' ' 1 $total_nodes`
total_nodes=0

for switch_i in `seq 1 10`; do
  switch_count[$switch_i]="0"
done

blue "Per-switch node count histogram for nodes {$apt_nodes}"
for node_i in $apt_nodes; do
	for switch_i in `seq 1 10`; do
		if [[ $(echo ${switch_map[$switch_i]} | grep -w "${nodename_prefix}$node_i") ]]; then
			switch_count[$switch_i]=`expr ${switch_count[$switch_i]} + 1`
			total_nodes=`expr $total_nodes + 1`
		fi
	done
done

for i in `seq 1 10`; do
	echo Switch $i: ${switch_count[$i]}
done

apt_nodes_size=`echo $apt_nodes | wc -w`
blue "$total_nodes of $apt_nodes_size in apt_nodes are accounted for"
blue "Node to switch map is available in $node_to_switch_map_file"
