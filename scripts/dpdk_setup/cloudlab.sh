#!/usr/bin/env bash
dpdk=~/dpdk

if [ ! -d "$dpdk" ]; then
  echo "DPDK directory "$dpdk" does not exist"
  exit
fi

# Load DPDK kernel modules
sudo modprobe uio

if [ ! -f "$dpdk"/x86_64-native-linuxapp-gcc/kmod/igb_uio.ko ]; then
  echo "igb_uio not found in $dpdk"
  exit
fi

sudo insmod "$dpdk"/x86_64-native-linuxapp-gcc/kmod/igb_uio.ko

# Bind CloudLab's experimental interface (10.*.*.*) to DPDK
(cd $(dirname $0); make ifname --silent)
ifname=`"$(dirname $0)"/ifname 10.`
echo "Binding interface "$ifname" to DPDK"

sudo ifconfig "$ifname" down
sudo "$dpdk"/usertools/dpdk-devbind.py --bind=igb_uio "$ifname"

# Create hugepage mount
# sudo mkdir -p /mnt/huge
# grep -s /mnt/huge /proc/mounts > /dev/null
# if [ $? -ne 0 ] ; then
# sudo mount -t hugetlbfs nodev /mnt/huge
# fi
