#!/usr/bin/env bash
dpdk=~/dpdk

# Load DPDK kernel modules
sudo modprobe uio
sudo insmod $dpdk/x86_64-native-linuxapp-gcc/kmod/igb_uio.ko

# Bind CloudLab's experimental interface (10.*.*.*) to DPDK
make ifname --silent
ifname=`./ifname 10.`
echo "Binding interface $ifname to DPDK"

sudo ifconfig $ifname down
sudo $dpdk/usertools/dpdk-devbind.py --bind=igb_uio $ifname

# Create hugepage mount
# sudo mkdir -p /mnt/huge
# grep -s /mnt/huge /proc/mounts > /dev/null
# if [ $? -ne 0 ] ; then
# sudo mount -t hugetlbfs nodev /mnt/huge
# fi
