#!/bin/bash
# Configure IP address for ens1f1 25 Gbps interface

# Extract the index X from hostname (akalianode-X.rdma.ron-pg0.utah.cloudlab.us)
host_index=`hostname | cut -d '.' -f 1 | cut -d '-' -f 2`
sudo ifconfig ens1f1 up
sudo ifconfig ens1f1 31.85.1.$host_index
