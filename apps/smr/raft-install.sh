#!/bin/bash
# willemt/raft does not have an install command
# Usage: Run in the willemt/raft folder
sudo mkdir -p /usr/local/include/raft
sudo cp include/* /usr/local/include/raft/
sudo cp libraft.a /usr/local/lib
sudo ldconfig
