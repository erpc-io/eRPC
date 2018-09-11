#!/bin/bash

# This creates the persistent memory Raft log file (/mnt/pmem12/raft_log).
#
# This assumes that there are two pmem regions. Check with:
#   sudo ipmctl show -region
#
# To configure the system with two regions:
#   sudo ipmctl create -goal MemoryMode=0
#   sudo reboot

# Use these if /dev/pmem12 and /dev/pmem13 are unavailable
#sudo ndctl create-namespace --mode fsdax --region 12
#sudo ndctl create-namespace --mode fsdax --region 13

# Format an ext4 filesystem because the device contains garbage:
sudo mkfs.ext4 /dev/pmem12

sudo mkdir /mnt/pmem12
sudo mount -o dax /dev/pmem12 /mnt/pmem12
sudo chmod --recursive ugo+rw /mnt/pmem12
fallocate -l 256G /mnt/pmem12/raft_log  # Some machines have 384 GB per NUMA
