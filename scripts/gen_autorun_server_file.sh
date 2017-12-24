#!/usr/bin/env bash
# Generate a basic autorun server file for Apt.

if [ "$#" -ne 1 ]; then
  echo "Illegal number of parameters"
	echo "Usage: ./gen_autorun_server_file.sh.sh <number of servers to generate>"
	exit
fi

# Generate server names for Apt
# $1: The number of servers to generate
function gen_apt() {
  server_id_list=`seq 1 $1`
  for server_i in $server_id_list; do
    echo akalianode-$server_i.RDMA.fawn.apt.emulab.net port:31850 numa:0 >> autorun_server_file
  done
}

rm -f autorun_server_file
gen_apt $1
