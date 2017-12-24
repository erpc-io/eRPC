#!/usr/bin/env bash
# Execute some command on all machines in autorun process file
source $(dirname $0)/autorun_parse.sh

for i in `seq 1 $autorun_num_processes`; do
  name=${autorun_name_list[$i]}
	echo "Executing check-nics.sh on $name"
	ssh -oStrictHostKeyChecking=no $name "ps -afx | grep main"

	#ssh -oStrictHostKeyChecking=no $name "cd ~/systemish/scripts/; sudo ./hugepages-create.sh 0 6000"
	#ssh -oStrictHostKeyChecking=no $name "cd ~/systemish/scripts/; ./hugepages-check.sh"
	#ssh -oStrictHostKeyChecking=no $name "ibv_devinfo | grep PORT_"
done
