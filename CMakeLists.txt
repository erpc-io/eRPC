cmake_minimum_required(VERSION 3.5)
project(eRPC)

set(CMAKE_CXX_COMPILER g++)
set(CMAKE_C_LINK_EXECUTABLE g++)

add_definitions(-std=c++11)

add_definitions(-Wall -Wextra -Werror)
add_definitions(-Wsign-conversion)
#add_definitions(-Winline)

add_definitions(-Wno-unused-function)

add_definitions(-march=native)
add_definitions(-g)

set(LIBRARIES ${LIBRARIES} rt numa ibverbs pthread)

# Testing
enable_testing()
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

include_directories(${CMAKE_SOURCE_DIR}/src)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# Debug mode
option(DEBUG "Enable debugging" OFF)
if(DEBUG)
  MESSAGE(WARNING "Debugging is enabled (to disable, run `cmake . -DDEBUG=OFF`).")
  add_definitions(-g)
else(DEBUG)
  MESSAGE(STATUS "Debugging is disabled (to enable, run `cmake . -DDEBUG=ON`).")
  add_definitions(-DNDEBUG)
  add_definitions(-O3)
endif(DEBUG)

# LTO
option(LTO "Enable link time optimization" OFF)
if(LTO)
  MESSAGE(WARNING "LTO is enabled (to disable, run `cmake . -DLTO=OFF`). Compilation will be slow.")
  add_definitions(-flto)
else(LTO)
  MESSAGE(STATUS "LTO is disabled (to enable, run `cmake . -DLTO=ON`).")
endif(LTO)

set(SOURCES
  src/common.h
  src/nexus.cc
  src/nexus.h
  src/rpc.h
  src/rpc_impl/rpc.cc
  src/rpc_impl/rpc_ev_loop.cc
  src/rpc_impl/rpc_connect_handlers.cc
  src/rpc_impl/rpc_disconnect_handlers.cc
  src/rpc_impl/rpc_sm_api.cc
  src/rpc_impl/rpc_sm_retry.cc
  src/session.cc
  src/session.h
  src/transport.cc
  src/transport.h
  src/transport_impl/ib_transport.cc
  src/transport_impl/ib_transport.h
  src/transport_types.h
  src/util/barrier.h
  src/util/buffer.h
  src/util/huge_alloc.cc
  src/util/huge_alloc.h
  src/util/rand.cc
  src/util/rand.h
  src/util/udp_client.cc
  src/util/udp_client.h)

if(LTO)
  # Using a compiled eRPC library with CMake and LTO is difficult.
  # See Evernote for details.
  add_executable(test_create_session tests/test_create_session.cc ${SOURCES})
  target_link_libraries(test_create_session ${GTEST_LIBRARIES} ${LIBRARIES})
  add_test(NAME test_create_session COMMAND test_create_session)

  add_executable(test_create_session_args tests/test_create_session_args.cc ${SOURCES})
  target_link_libraries(test_create_session_args ${GTEST_LIBRARIES} ${LIBRARIES})
  add_test(NAME test_create_session_args COMMAND test_create_session_args)

  add_executable(test_destroy_session tests/test_destroy_session.cc ${SOURCES})
  target_link_libraries(test_destroy_session ${GTEST_LIBRARIES} ${LIBRARIES})
  add_test(NAME test_destroy_session COMMAND test_destroy_session)

  add_executable(test_huge_alloc tests/test_huge_alloc.cc ${SOURCES})
  target_link_libraries(test_huge_alloc ${GTEST_LIBRARIES} ${LIBRARIES})

  add_executable(test_rand tests/test_rand.cc ${SOURCES})
  target_link_libraries(test_rand ${GTEST_LIBRARIES} ${LIBRARIES})

  add_executable(test_fixed_vector tests/test_fixed_vector.cc ${SOURCES})
  target_link_libraries(test_fixed_vector ${GTEST_LIBRARIES} ${LIBRARIES})
else(LTO)
  add_library(erpc ${SOURCES})

  add_executable(test_create_session tests/test_create_session.cc)
  target_link_libraries(test_create_session erpc ${GTEST_LIBRARIES} ${LIBRARIES})
  add_test(NAME test_create_session COMMAND test_create_session)

  add_executable(test_create_session_args tests/test_create_session_args.cc)
  target_link_libraries(test_create_session_args erpc ${GTEST_LIBRARIES} ${LIBRARIES})
  add_test(NAME test_create_session_args COMMAND test_create_session_args)

  add_executable(test_destroy_session tests/test_destroy_session.cc)
  target_link_libraries(test_destroy_session erpc ${GTEST_LIBRARIES} ${LIBRARIES})
  add_test(NAME test_destroy_session COMMAND test_destroy_session)

  add_executable(test_huge_alloc tests/test_huge_alloc.cc)
  target_link_libraries(test_huge_alloc erpc ${GTEST_LIBRARIES} ${LIBRARIES})

  add_executable(test_rand tests/test_rand.cc)
  target_link_libraries(test_rand erpc ${GTEST_LIBRARIES} ${LIBRARIES})

  add_executable(test_fixed_vector tests/test_fixed_vector.cc)
  target_link_libraries(test_fixed_vector ${GTEST_LIBRARIES} ${LIBRARIES})
endif(LTO)
