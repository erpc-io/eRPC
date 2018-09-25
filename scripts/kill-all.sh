#!/usr/bin/env bash
# Kill autorun_app on all machines in autorun process file
source $(dirname $0)/autorun_parse.sh

blue "Killing $autorun_app everywhere"

for ((i = 0; i < $autorun_num_processes; i++)); do
  name=${autorun_name_list[$i]}
	ssh -oStrictHostKeyChecking=no $name "sudo pkill $autorun_app 1>/dev/null 2>/dev/null" &
done
wait

blue "Printing $autorun_app processes that are still running..."
for ((i = 0; i < $autorun_num_processes; i++)); do
  (
	ret=`ssh -oStrictHostKeyChecking=no ${autorun_name_list[$i]} "pgrep -x $autorun_app"`
  if [ -n "$ret" ]; then
    echo "$autorun_app still running on ${autorun_name_list[$i]}"
  fi
  ) &
done
wait
