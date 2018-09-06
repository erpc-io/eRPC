#!/bin/bash
# willemt/raft's Makefile contains flags for code coverage that we don't need.
# This compiles a more optimized library.
#
# Usage: Run in the willemt/raft folder
make CFLAGS='-Iinclude -Werror -Werror=return-type -Werror=uninitialized \
  -Wcast-align -Wno-pointer-sign -fno-omit-frame-pointer -fno-common \
  -fsigned-char -I CLinkedListQueue/ -g -O3 -fPIC -march=native'

