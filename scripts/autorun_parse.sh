#!/usr/bin/env bash
# Requirement: A file named autorun_process_file in this script's directory
# that contains neprocessseparated, DNS-resolvable node names.
source $(dirname $0)/utils.sh

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

# autorun_app must be defined
check_env "autorun_app"

# Variables set by the human user
autorun_erpc_home="$HOME/eRPC"

# Variables exported by this script
autorun_out_prefix="/tmp/${autorun_app}_out"
autorun_err_prexif="/tmp/${autorun_app}_err"
autorun_stat_prefix="/tmp/${autorun_app}_stats"

autorun_test_ms=`get_from_config "test_ms"`
autorun_num_processes=`get_from_config "num_processes"`

# Create autorun process arrays using the first $autorun_num_processes process
# names in autorun_process_file.
autorun_process_name_list[0]=""
autorun_process_udp_port_list[0]=""
autorun_process_numa_node_list[0]=""
server_idx="0"

autorun_process_file=$(dirname $0)/autorun_process_file
if [ ! -f $autorun_process_file ]; then
  blue "autorun: Server file not found. Exiting."
  exit
fi

# Check if the process file has sufficient processs
autorun_process_file_num_processes=`cat $autorun_process_file | wc -l`
if [ "$autorun_process_file_num_processes" -lt  "$autorun_num_processes" ]; then
  blue "auorun: Too few processs in process file. Exiting."
  exit
fi

while read dns_name udp_port numa_node; do
  autorun_process_name_list[$server_idx]=$dns_name
  autorun_process_udp_port_list[$server_idx]=$udp_port
  autorun_process_numa_node_list[$server_idx]=$numa_node
  ((col_index+=1))
done < $autorun_process_file

blue "autorun: app = $autorun_app, test ms = $autorun_test_ms, num_processes = $autorun_num_processes"
