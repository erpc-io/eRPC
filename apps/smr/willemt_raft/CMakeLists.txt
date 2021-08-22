cmake_minimum_required(VERSION 2.8)
project(willemt_raft)

set(SOURCES
  src/raft_log.c
  src/raft_node.c
  src/raft_server.c
  src/raft_server_properties.c)

include_directories(include)
add_library(raft ${SOURCES})
