#!/usr/bin/env bash
# Script to set up eRPC on Azure

sudo bash -c "echo 2048 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages"
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge

sudo modprobe ib_uverbs
sudo modprobe mlx4_ib
