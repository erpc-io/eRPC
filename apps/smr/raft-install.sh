#!/bin/bash
# 
# willemt/raft does not have an install command, so use this.
#
# willemt/raft's Makefile contains flags for code coverage that cause slowness.
# This compiles a more optimized library by overriding the Makefile's CFLAGS.

rm -rf /tmp/raft
git clone https://github.com/willemt/raft.git /tmp/raft

cd /tmp/raft
make CFLAGS='-Iinclude -Werror -Werror=return-type -Werror=uninitialized \
  -Wcast-align -Wno-pointer-sign -fno-omit-frame-pointer -fno-common \
  -fsigned-char -I CLinkedListQueue/ -g -O3 -DNDEBUG -fPIC -march=native'

sudo mkdir -p /usr/local/include/raft
sudo cp include/* /usr/local/include/raft/
sudo cp libraft.a /usr/local/lib

sudo ldconfig
