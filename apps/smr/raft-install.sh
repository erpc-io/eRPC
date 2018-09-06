#!/bin/bash
#
# willemt/raft's Makefile contains flags for code coverage that we don't need.
# This compiles a more optimized library.
#
# willemt/raft does not have an install command. This does.

rm -rf /tmp/raft
git clone https://github.com/willemt/raft.git /tmp/raft

cd /tmp/raft
make CFLAGS='-Iinclude -Werror -Werror=return-type -Werror=uninitialized \
  -Wcast-align -Wno-pointer-sign -fno-omit-frame-pointer -fno-common \
  -fsigned-char -I CLinkedListQueue/ -g -O3 -fPIC -march=native'

sudo mkdir -p /usr/local/include/raft
sudo cp include/* /usr/local/include/raft/
sudo cp libraft.a /usr/local/lib

sudo ldconfig
