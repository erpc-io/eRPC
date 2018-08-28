#!/usr/bin/env bash
# Script to bind all experimental interfaces to DPDK on any CloudLab cluster.
# This works because CloudLab assigns 10.* IP addresses to all experimental
# interfaces.
sudo modprobe uio
sudo modprobe igb_uio

# Bind CloudLab's experimental interface (10.*.*.*) to DPDK
if [ ! -f "$(dirname $0)"/ifname ]; then
  echo "Error: ifname not found in "$(dirname $0)""
  exit
fi

exp_interfaces=`"$(dirname $0)"/ifname 10.`
echo "Binding interfaces $exp_interfaces to DPDK"

for ifname in $exp_interfaces; do
  sudo ifconfig "$ifname" down
  sudo dpdk-devbind --bind=igb_uio "$ifname"
done

# Create hugepage mount
# sudo mkdir -p /mnt/huge
# grep -s /mnt/huge /proc/mounts > /dev/null
# if [ $? -ne 0 ] ; then
# sudo mount -t hugetlbfs nodev /mnt/huge
# fi
