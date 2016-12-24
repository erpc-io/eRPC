cmake_minimum_required(VERSION 3.5)
project(eRPC)

set(CMAKE_CXX_COMPILER g++-5)
set(CMAKE_C_LINK_EXECUTABLE g++-5)

add_definitions(-std=c++11)

add_definitions(-Wall -Wextra)
add_definitions(-Wsign-conversion)
add_definitions(-Winline)

add_definitions(-march=native)

add_definitions(-g)

set(LIBRARIES ${LIBRARIES} rt numa ibverbs pthread)

# Locate GTest
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

include_directories(${CMAKE_SOURCE_DIR}/src)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

option(DEBUG "Enable debugging" OFF)
if(DEBUG)
  MESSAGE(WARNING "Debugging is ENABLED (to disable, run `cmake .. -DDEBUG=OFF`). Performance will be low.")
  add_definitions(-Og)
else(DEBUG)
  MESSAGE(STATUS "Debugging is disabled (to enable, run `cmake .. -DDEBUG=ON`)")
  add_definitions(-DNDEBUG)
  add_definitions(-O3)
endif(DEBUG)

set(SOURCES
  src/common.h
  src/huge_alloc.h
  src/rpc.h
  src/session.h
  src/transport.h
  src/transport_impl/infiniband_transport.h
  src/transport_types.h)

add_executable(test_huge_alloc tests/test_huge_alloc.cc ${SOURCE_FILES})
target_link_libraries(test_huge_alloc ${GTEST_LIBRARIES} ${LIBRARIES})
