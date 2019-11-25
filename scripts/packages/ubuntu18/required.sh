#!/bin/bash
#
# Set up a fresh Ubuntu 18.04 box with only essential scripts for building the
# eRPC library. This does not install application-specific or
# dev environment packages.

sudo apt update

# Toolchain
sudo apt -y install g++-8 cmake

# General libs
sudo apt -y install libnuma-dev libgflags-dev libgtest-dev libboost-dev
(cd /usr/src/gtest && sudo cmake . && sudo make && sudo mv libg* /usr/lib/)

# DPDK drivers
# Latest DPDK: sudo make install T=x86_64-native-linuxapp-gcc DESTDIR=/usr
# sudo apt -y install dpdk libdpdk-dev dpdk-igb-uio-dkms

# Verbs drivers must be installed from Mellanox OFED
