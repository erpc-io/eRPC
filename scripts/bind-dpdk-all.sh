#!/usr/bin/env bash
# Bind the experimental CloudLab interface to DPDK on all machines
source $(dirname $0)/autorun_parse.sh

for ((i = 0; i < $autorun_num_processes; i++)); do
  name=${autorun_name_list[$i]}
  ssh -oStrictHostKeyChecking=no $name "\
    cd $autorun_erpc_home; sudo ./scripts/setup_dpdk/cloudlab.sh" &
done
wait

blue "Printing dpdk-devbind results on all nodes..."
for ((i = 0; i < $autorun_num_processes; i++)); do
  ret=`ssh -oStrictHostKeyChecking=no ${autorun_name_list[$i]} "\
    dpdk-devbind -s | grep drv=igb_uio"`
  echo "Machine ${autorun_name_list[$i]}: $ret"
  if [ -z "$ret" ]; then
    echo "No DPDK port available on ${autorun_name_list[$i]}"
  fi
done

wait
