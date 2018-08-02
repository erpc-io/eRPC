#!/usr/bin/env bash
dpdk=~/dpdk

# Load DPDK kernel modules
sudo modprobe uio
sudo insmod $dpdk/x86_64-native-linuxapp-gcc/kmod/igb_uio.ko

# Bind 10 GbE to DPDK
sudo ifconfig enp3s0f0 down
sudo $dpdk/usertools/dpdk-devbind.py --bind=igb_uio enp3s0f0

# Create hugepage mount
# sudo mkdir -p /mnt/huge
# grep -s /mnt/huge /proc/mounts > /dev/null
# if [ $? -ne 0 ] ; then
# sudo mount -t hugetlbfs nodev /mnt/huge
# fi
