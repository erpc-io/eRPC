#!/usr/bin/env bash
# Script to bind an Intel Ethernet NIC to DPDK on any CloudLab cluster

sudo modprobe uio igb_uio

# Bind CloudLab's experimental interface (10.*.*.*) to DPDK
(cd $(dirname $0); rm ifname; g++ -std=c++11 -o ifname ifname.cc)
ifname=`"$(dirname $0)"/ifname 10.`
echo "Binding interface "$ifname" to DPDK"

sudo ifconfig "$ifname" down
sudo dpdk-devbind --bind=igb_uio "$ifname"

# Create hugepage mount
# sudo mkdir -p /mnt/huge
# grep -s /mnt/huge /proc/mounts > /dev/null
# if [ $? -ne 0 ] ; then
# sudo mount -t hugetlbfs nodev /mnt/huge
# fi
