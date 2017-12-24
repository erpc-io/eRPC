#!/usr/bin/env bash
# Kill autorun_app on all autorun_nodes
source $(dirname $0)/autorun.sh

blue "Killing $autorun_app on all autorun nodes"

for node in $autorun_nodes; do
	ssh -oStrictHostKeyChecking=no $node "sudo killall $autorun_app 1>/dev/null 2>/dev/null" &
done
wait

blue "Checking if $autorun_app is still running on any node"
for node in $autorun_nodes; do
  (
	ret=`ssh -oStrictHostKeyChecking=no $node "pgrep -x $autorun_app"`
  if [ -n "$ret" ]; then
    echo "$autorun_app still running on $node"
  fi
  ) &
done
wait
