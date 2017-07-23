#!/usr/bin/env bash
# Requirement: A file named autorun_node_file in this script's directory
# that contains newline-separated, DNS-resolvable node names.
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
autorun_erpc_home="/users/akalia/eRPC"

# Variables exported by this script
autorun_out_file="/tmp/${autorun_app}_out"
autorun_err_file="/tmp/${autorun_app}_err"
autorun_stat_file="/tmp/${autorun_app}_stats"
autorun_test_ms=`get_from_config "test_ms"`
autorun_num_nodes=`get_from_config "num_machines"`

# Create autorun_nodes using the first $autorun_num_nodes node names in
# autorun_node_file. 
autorun_nodes=""
autorun_node_file=$(dirname $0)/autorun_node_file

if [ ! -f $autorun_node_file ]; then
  blue "autorun: Node file not found. Exiting."
  exit
fi

# Check if the node file has sufficient nodes
autorun_node_file_num_nodes=`cat $autorun_node_file | wc -l`
if [ "$autorun_node_file_num_nodes" -lt  "$autorun_num_nodes" ]; then
  blue "auorun: Too few nodes in node file. Exiting."
  exit
fi

autorun_nodes=`cat $autorun_node_file | head -$autorun_num_nodes | tr '\n' ' '`

blue "autorun: app = $autorun_app, test ms = $autorun_test_ms, num nodes = $autorun_num_nodes"
