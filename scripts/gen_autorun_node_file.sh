#!/usr/bin/env bash
# Generate a basic autorun node file.

if [ "$#" -ne 1 ]; then
  echo "Illegal number of parameters"
	echo "Usage: ./gen_autorun_node_file.sh.sh <number of nodes to generate>"
	exit
fi

# Generate node names for Apt
function gen_apt() {
  nodes=`seq 1 $1`
  for node_i in $nodes; do
    echo akalianode-$node_i.RDMA.fawn.apt.emulab.net >> autorun_node_file
  done
}

rm -f autorun_node_file
gen_apt $1
