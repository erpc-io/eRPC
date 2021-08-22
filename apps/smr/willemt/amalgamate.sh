# Copyright (c) 2013, Willem-Hendrik Thiart
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# 
# @file
# @brief Implementation of a Raft server
# @author Willem Thiart himself@willemthiart.com


#!/bin/bash

# The raft.h file here was created using willemt/raft commit e428eeb
#
# I had to modify the amalgamate.sh script in willemt/raft to the commands
# below to fix compilation issues.

echo '/*

This source file is the amalgamated version of the original.
Please see github.com/willemt/raft for the original version.
'
git log | head -n1 | sed 's/commit/HEAD commit:/g'
echo '
'
cat LICENSE
echo '
*/
'

echo '
#ifndef RAFT_AMALGAMATION_SH
#define RAFT_AMALGAMATION_SH
'

cat include/raft_types.h | sed 's/#include "raft.*.h"//g' 
cat include/raft.h | sed 's/#include "raft.*.h"//g' 
cat include/raft_private.h | sed 's/#include "raft.*.h"//g' 
cat include/raft_log.h | sed 's/#include "raft.*.h"//g' 
cat src/raft*.c | sed 's/#include "raft.*.h"//g' | sed 's/__log/__raft_console_log/g' | sed 's/\bmax\b/willemt_raft_max/g' | sed 's/\bmin\b/willemt_raft_min/g'

echo '#endif /* RAFT_AMALGAMATIONE_SH */'
