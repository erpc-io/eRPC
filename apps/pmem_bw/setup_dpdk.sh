#!/usr/bin/env bash
dpdk=~/sandbox/dpdk-19.08/

sudo modprobe uio
sudo insmod $dpdk/x86_64-native-linux-gcc/kmod/igb_uio.ko

# Create hugepage mount
sudo mkdir -p /mnt/huge
grep -s /mnt/huge /proc/mounts > /dev/null

if [ $? -ne 0 ] ; then
  sudo mount -t hugetlbfs nodev /mnt/huge
fi

# Bind IOAT devices on NUMA node 0, choose igb_uio (userspace) or ioatdma (kernel)
for i in `seq 0 7`; do
  sudo ${dpdk}/usertools/dpdk-devbind.py -b igb_uio 0000:00:04.$i
done

# Bind IOAT devices on NUMA node 1, choose igb_uio (userspace) or ioatdma (kernel)
for i in `seq 0 7`; do
  sudo ${dpdk}/usertools/dpdk-devbind.py -b ioatdma 0000:80:04.$i
done
