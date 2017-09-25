cmake_minimum_required(VERSION 3.5)
project(eRPC)

if(NOT DEFINED ENV{autorun_app})
  MESSAGE(FATAL_ERROR "Please set environment variable autorun_app")
endif()

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
include_directories(${CMAKE_SOURCE_DIR}/third_party)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# Compile the ERPC library unless we cannot (profile-guided optimization or LTO)
set(COMPILE_ERPC_LIB ON)

# Options exposed to the user
option(PERF "Compile for performance" OFF)
set(PGO "none" CACHE STRING "Profile-guided optimization (generate/use/none)")
set(LOG_LEVEL "warn" CACHE STRING "Logging level (none/error/warn/info/debug/trace)") 

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

# Profile-guided optimization
if(PGO STREQUAL "generate")
  MESSAGE(STATUS "Profile-guided optimization (generate mode) is enabled. Performance will be low.")
  add_definitions(-fprofile-generate)
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-generate")
  SET(COMPILE_ERPC_LIB OFF)
elseif(PGO STREQUAL "use")
  MESSAGE(STATUS "Profile-guided optimization (use mode) is enabled.")
  add_definitions(-fprofile-use -fprofile-correction)
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-use -fprofile-correction")
elseif(PGO STREQUAL "none")
  MESSAGE(STATUS "Profile-guided optimization is disabled.")
endif()

# Logging level
if(LOG_LEVEL STREQUAL "none")
  MESSAGE(STATUS "Logging level = none.")
  add_definitions(-DLOG_LEVEL=0)
elseif(LOG_LEVEL STREQUAL "error")
  MESSAGE(STATUS "Logging level = error.")
  add_definitions(-DLOG_LEVEL=1)
elseif(LOG_LEVEL STREQUAL "warn")
  MESSAGE(STATUS "Logging level = warn.")
  add_definitions(-DLOG_LEVEL=2)
elseif(LOG_LEVEL STREQUAL "info")
  MESSAGE(STATUS "Logging level = info.")
  add_definitions(-DLOG_LEVEL=3)
elseif(LOG_LEVEL STREQUAL "debug")
  MESSAGE(STATUS "Logging level = debug. Warning: Performance will be low.")
  add_definitions(-DLOG_LEVEL=4)
elseif(LOG_LEVEL STREQUAL "trace")
  MESSAGE(STATUS "Logging level = trace. Warning: Performance will be low.")
  add_definitions(-DLOG_LEVEL=5)
else()
  MESSAGE(STATUS "No logging level specified. Using warning level.")
  add_definitions(-DLOG_LEVEL=2)
endif()

# Debug mode
if(DEBUG)
  MESSAGE(STATUS "Debugging is enabled. Perf will be low.")
  add_definitions(-g)
else(DEBUG)
  MESSAGE(STATUS "Debugging is disabled.")
  add_definitions(-DNDEBUG)
  add_definitions(-O2)
endif(DEBUG)

# Fault injection (packet losses, machine failure)
if(FAULT_INJECTION)
  MESSAGE(STATUS "Fault injection is enabled. Performance will be low.")
  add_definitions(-DFAULT_INJECTION)
else(FAULT_INJECTION)
  MESSAGE(STATUS "Fault injection is disabled, so tests may fail.")
endif(FAULT_INJECTION)

# LTO
if(LTO)
  MESSAGE(STATUS "LTO is enabled. Tests won't be compiled.")
  SET(COMPILE_ERPC_LIB OFF)
  add_definitions(-flto)
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
else(LTO)
  MESSAGE(STATUS "LTO is disabled. Performance will be low.")
endif(LTO)

set(SOURCES
  src/nexus_impl/nexus.cc
  src/nexus_impl/nexus_bg_thread.cc
  src/nexus_impl/nexus_sm_thread.cc
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
  src/rpc_impl/rpc_reset_handlers.cc
  src/rpc_impl/rpc_sm_api.cc
  src/rpc_impl/rpc_sm_helpers.cc
  src/session.cc
  src/transport.cc
  src/transport_impl/ib_transport.cc
  src/transport_impl/ib_transport_datapath.cc
  src/util/huge_alloc.cc
  src/util/tls_registry.cc)

# MICA sources
set(MICA_SOURCES
  mica/src/mica/table/fixedtable_impl/get.cc
  mica/src/mica/table/fixedtable_impl/del.cc
  mica/src/mica/table/fixedtable_impl/bucket.cc
  mica/src/mica/table/fixedtable_impl/set.cc
  mica/src/mica/table/fixedtable_impl/prefetch.cc
  mica/src/mica/table/fixedtable_impl/init.cc
  mica/src/mica/table/fixedtable_impl/lock.cc
  mica/src/mica/table/fixedtable_impl/info.cc
  mica/src/mica/table/fixedtable_impl/item.cc
  mica/src/mica/util/cityhash/city_mod.cc
  mica/src/mica/util/config.cc)

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

# Compile the tests
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
endif()

# The app to compile (small_rpc_tput, large_rpc_tput, consensus, masstree_analytics)
set(APP $ENV{autorun_app})

# Add app-specific defines now, isolating them from the tests
if(APP STREQUAL "consensus")
  # Raft library from willemt/raft, installed at system-level
  set(LIBRARIES ${LIBRARIES} craft)
  set(APP_ADDITIONAL_SOURCES third_party/cityhash/city_mod.cc)
  # MICA
  include_directories(mica/src)
  set(APP_ADDITIONAL_SOURCES ${MICA_SOURCES})
elseif(APP STREQUAL "masstree_analytics")
  # CMake-based Masstree library from anujkaliaiitd/masstree-beta
  link_directories($ENV{HOME}/masstree-beta)
  include_directories($ENV{HOME}/masstree-beta)
  add_definitions(-include $ENV{HOME}/masstree-beta/config.h)
  set(APP_ADDITIONAL_SOURCES
    third_party/cityhash/city_mod.cc
    apps/masstree_analytics/mt_index_api.cc)
  set(LIBRARIES ${LIBRARIES} masstree)
elseif(APP STREQUAL "mica_test")
  include_directories(mica/src)
  set(APP_ADDITIONAL_SOURCES ${MICA_SOURCES})
endif()

# Compile only the one application. If COMPILE_ERPC_LIB is false, we use LTO
# for the app. LTO is hard to do with a compiled library.
if(COMPILE_ERPC_LIB)
  add_executable(${APP} apps/${APP}/${APP}.cc ${APP_ADDITIONAL_SOURCES})
  target_link_libraries(${APP} erpc ${GTEST_LIBRARIES} ${LIBRARIES})
else(COMPILE_ERPC_LIB)
  add_executable(${APP} apps/${APP}/${APP}.cc ${APP_ADDITIONAL_SOURCES} ${SOURCES})
  target_link_libraries(${APP} ${GTEST_LIBRARIES} ${LIBRARIES})
endif(COMPILE_ERPC_LIB)
