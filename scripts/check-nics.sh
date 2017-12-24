#!/usr/bin/env bash
# Execute some command on all nodes in $autorun_nodes
source $(dirname $0)/autorun.sh

for node in $autorun_nodes; do
	# $node is an address that we can ssh to
	echo "Executing check-nics.sh on $node"

## GRUB update
	#echo "Copying grub to $node"
	#scp /etc/default/grub $node:.

	#echo "Moving grub to /etc/default/grub and updating;"
	#ssh -oStrictHostKeyChecking=no $node "sudo mv grub /etc/default/grub; sudo update-grub; sudo reboot"
##

	#ssh -oStrictHostKeyChecking=no $node "cd ~/systemish/scripts/; sudo ./hugepages-create.sh 0 6000"
	#ssh -oStrictHostKeyChecking=no $node "cd ~/systemish/scripts/; ./hugepages-check.sh"
	#ssh -oStrictHostKeyChecking=no $node "ibv_devinfo | grep PORT_"
	ssh -oStrictHostKeyChecking=no $node "ps -afx | grep main"
done
