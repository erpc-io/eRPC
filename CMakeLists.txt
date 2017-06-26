cmake_minimum_required(VERSION 3.5)
project(eRPC)

set(CMAKE_CXX_COMPILER g++-7)
set(CMAKE_C_LINK_EXECUTABLE g++-7)

add_definitions(-std=c++14)

add_definitions(-Wall -Wextra -Werror)
add_definitions(-Wsign-conversion)
add_definitions(-Wold-style-cast)

add_definitions(-Wno-unused-function)

add_definitions(-march=native)
add_definitions(-g)

set(LIBRARIES ${LIBRARIES} rt numa ibverbs pthread enet gflags papi)

# Testing
enable_testing()
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

include_directories(${CMAKE_SOURCE_DIR}/src)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# Compile the ERPC library unless we cannot (profile-guided optimization or LTO)
set(COMPILE_ERPC_LIB ON)

# Options exposed to the user
option(PERF "Compile for performance" OFF)
set(PGO "none" CACHE STRING "Profile-guided optimization (generate/use)")

# Parse the user-exposed options
if(PERF)
  MESSAGE(STATUS "Compilation optimized for performance.")
  SET(DEBUG OFF)
  SET(LTO ON)
  SET(FAULT_INJECTION OFF)
else(PERF)
  MESSAGE(STATUS "Compilation not optimized for performance.")
  SET(DEBUG ON)
  SET(LTO OFF)
  SET(FAULT_INJECTION ON)
endif(PERF)

if(PGO STREQUAL "generate")
  MESSAGE(STATUS "Profile-guided optimization (generate mode) enabled. Performance will be low.")
  add_definitions(-fprofile-generate)
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-generate")
  SET(COMPILE_ERPC_LIB OFF)
elseif(PGO STREQUAL "use")
  MESSAGE(STATUS "Profile-guided optimization (use mode) enabled.")
  add_definitions(-fprofile-use -fprofile-correction)
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-use -fprofile-correction")
elseif(PGO STREQUAL "none")
  MESSAGE(STATUS "Profile-guided optimization disabled.")
endif()

# Debug mode
option(DEBUG "Enable debugging" ON)
if(DEBUG)
  MESSAGE(STATUS "Debugging is enabled (to disable, run `cmake . -DDEBUG=OFF`).")
  add_definitions(-g)
else(DEBUG)
  MESSAGE(STATUS "Debugging is disabled (to enable, run `cmake . -DDEBUG=ON`).")
  add_definitions(-DNDEBUG)
  add_definitions(-O3)
endif(DEBUG)

# Fault injection (packet losses, machine failure)
option(FAULT_INJECTION "Enable fault injection" ON)
if(FAULT_INJECTION)
  MESSAGE(STATUS "Fault injection is enabled. Performance will be low. To disable, run `cmake . -DFAULT_INJECTION=OFF`.")
  add_definitions(-DFAULT_INJECTION)
else(FAULT_INJECTION)
  MESSAGE(STATUS "Fault injection is disabled, so tests may fail. To enable, run `cmake . -DFAULT_INJECTION=ON`.")
endif(FAULT_INJECTION)

# LTO
option(LTO "Enable link time optimization" OFF)
if(LTO)
  MESSAGE(STATUS "LTO is enabled. Tests won't be compiled. To disable, run `cmake . -DLTO=OFF`.")
  SET(COMPILE_ERPC_LIB OFF)
  add_definitions(-flto)
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
else(LTO)
  MESSAGE(STATUS "LTO is disabled. To enable, run `cmake . -DLTO=ON`.")
endif(LTO)

set(SOURCES
  src/common.h
  src/nexus_impl/nexus.cc
  src/nexus_impl/nexus_bg_thread.cc
  src/nexus_impl/nexus_sm_thread.cc
  src/nexus.h
  src/msg_buffer.h
  src/ops.h
  src/pkthdr.h
  src/rpc.h
  src/rpc_impl/rpc.cc
  src/rpc_impl/rpc_bg_queues.cc
  src/rpc_impl/rpc_send_rfr.cc
  src/rpc_impl/rpc_send_cr.cc
  src/rpc_impl/rpc_enqueue_request.cc
  src/rpc_impl/rpc_enqueue_response.cc
  src/rpc_impl/rpc_ev_loop.cc
  src/rpc_impl/rpc_fault_inject.cc
  src/rpc_impl/rpc_pkt_loss.cc
  src/rpc_impl/rpc_rx.cc
  src/rpc_impl/rpc_tx.cc
  src/rpc_impl/rpc_connect_handlers.cc
  src/rpc_impl/rpc_disconnect_handlers.cc
  src/rpc_impl/rpc_sm_api.cc
  src/rpc_impl/rpc_sm_helpers.cc
  src/session.cc
  src/session.h
  src/transport.cc
  src/transport.h
  src/transport_impl/ib_transport.cc
  src/transport_impl/ib_transport_datapath.cc
  src/transport_impl/ib_transport.h
  src/util/barrier.h
  src/util/buffer.h
  src/util/huge_alloc.cc
  src/util/huge_alloc.h
  src/util/misc.h
  src/util/mt_list.h
  src/util/math_utils.h
  src/util/rand.h
  src/util/tls_registry.h
  src/util/tls_registry.cc
  src/util/timer.h
  tests/test_basics.h)

# The tests to run using ctest
set(CTEST_TESTS
  test_create_session
  test_create_session_args
  test_destroy_session
  test_api_restrictions
  test_small_msg
  test_large_msg
  test_req_in_cont_func
  test_req_in_req_func
  test_packet_loss)

# Other tests that are run without ctest
set(OTHER_TESTS
  test_huge_alloc
  test_rand
  test_fixed_vector)

set(APPS
  small_rpc_tput)

if(COMPILE_ERPC_LIB)
  MESSAGE(STATUS "Compiling ERPC as a library")
  add_library(erpc ${SOURCES})

  foreach(test_name IN LISTS CTEST_TESTS)
    add_executable(${test_name} tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${GTEST_LIBRARIES} ${LIBRARIES})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  foreach(test_name IN LISTS OTHER_TESTS)
    add_executable(${test_name} tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${GTEST_LIBRARIES} ${LIBRARIES})
  endforeach()

  foreach(app_name IN LISTS APPS)
    add_executable(${app_name} apps/${app_name}/${app_name}.cc)
    target_link_libraries(${app_name} erpc ${GTEST_LIBRARIES} ${LIBRARIES})
  endforeach()
else(COMPILE_ERPC_LIB)
  # Using a compiled eRPC library with CMake and LTO is difficult.
  # See Evernote for details.
  MESSAGE(STATUS "Not compiling ERPC as a library")

  foreach(app_name IN LISTS APPS)
    add_executable(${app_name} apps/${app_name}/${app_name}.cc ${SOURCES})
    target_link_libraries(${app_name} ${GTEST_LIBRARIES} ${LIBRARIES})
  endforeach()
endif(COMPILE_ERPC_LIB)
