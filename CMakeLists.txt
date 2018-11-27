cmake_minimum_required(VERSION 2.8)
project(eRPC)

include(CMakeDependentOption)

# Pick a compiler
#set(CMAKE_CXX_COMPILER clang++-6.0)
#set(CMAKE_CXX_COMPILER g++-8)
set(CMAKE_CXX_COMPILER g++)

add_definitions(-std=c++11 -march=native -g)
add_definitions(-Wall -Wextra -Werror -pedantic -Wsign-conversion -Wold-style-cast)
add_definitions(-Wno-unused-function -Wno-nested-anon-types -Wno-keyword-macro)

set(LIBRARIES ${LIBRARIES} rt numa pthread gflags)

# Unit tests
enable_testing()
find_package(GTest REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/third_party)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# liberpc will be compiled unless profile-guided optimization or LTO is enabled
set(COMPILE_ERPC_LIB ON)

# Options exposed to the user
set(TRANSPORT "dpdk" CACHE STRING "Datapath transport (infiniband/raw/dpdk)")
option(ROCE "Use RoCE if TRANSPORT is infiniband" OFF)
option(PERF "Compile for performance" OFF)
set(PGO "none" CACHE STRING "Profile-guided optimization (generate/use/none)")
set(LOG_LEVEL "warn" CACHE STRING "Logging level (none/error/warn/info/reorder/trace/cc)") 
cmake_dependent_option(LTO "Use link time optimization" ON "PERF" OFF)

# Parse the user-exposed options
if(PERF)
  MESSAGE(STATUS "Compilation optimized for performance.")
  SET(DEBUG OFF)
  SET(TESTING OFF)
else(PERF)
  MESSAGE(STATUS "Compilation not optimized for performance.")
  SET(DEBUG ON)
  SET(TESTING ON)
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
elseif(LOG_LEVEL STREQUAL "reorder")
  MESSAGE(STATUS "Logging level = reorder. Warning: Performance will be low.")
  add_definitions(-DLOG_LEVEL=4)
elseif(LOG_LEVEL STREQUAL "trace")
  MESSAGE(STATUS "Logging level = trace. Warning: Performance will be low.")
  add_definitions(-DLOG_LEVEL=5)
elseif(LOG_LEVEL STREQUAL "cc")
  MESSAGE(STATUS "Logging level = cc. Warning: Performance will be low.")
  add_definitions(-DLOG_LEVEL=6)
else()
  MESSAGE(STATUS "No logging level specified. Using warning level.")
  add_definitions(-DLOG_LEVEL=2)
endif()

# Debug mode
if(DEBUG)
  MESSAGE(STATUS "Debugging is enabled. Perf will be low.")
else(DEBUG)
  MESSAGE(STATUS "Debugging is disabled.")
  add_definitions(-DNDEBUG)
  add_definitions(-O2)
endif(DEBUG)

# Testing for packet loss, machine failure, etc
if(TESTING)
  MESSAGE(STATUS "Testing is enabled. Performance will be low.")
  add_definitions(-DTESTING=true)
else(TESTING)
  MESSAGE(STATUS "Testing is disabled, so tests may fail.")
  add_definitions(-DTESTING=false)
endif(TESTING)

# Link-time optimization
if(LTO)
  MESSAGE(STATUS "LTO is enabled. eRPC library won't be compiled.")
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
  src/rpc_impl/rpc_queues.cc
  src/rpc_impl/rpc_rfr.cc
  src/rpc_impl/rpc_cr.cc
  src/rpc_impl/rpc_kick.cc
  src/rpc_impl/rpc_req.cc
  src/rpc_impl/rpc_resp.cc
  src/rpc_impl/rpc_ev_loop.cc
  src/rpc_impl/rpc_fault_inject.cc
  src/rpc_impl/rpc_pkt_loss.cc
  src/rpc_impl/rpc_rx.cc
  src/rpc_impl/rpc_connect_handlers.cc
  src/rpc_impl/rpc_disconnect_handlers.cc
  src/rpc_impl/rpc_reset_handlers.cc
  src/rpc_impl/rpc_sm_api.cc
  src/rpc_impl/rpc_sm_helpers.cc
  src/transport_impl/transport.cc
  src/transport_impl/dpdk/dpdk_transport.cc
  src/transport_impl/dpdk/dpdk_transport_datapath.cc
  src/transport_impl/infiniband/ib_transport.cc
  src/transport_impl/infiniband/ib_transport_datapath.cc
  src/transport_impl/raw/raw_transport.cc
  src/transport_impl/raw/raw_transport_datapath.cc
  src/util/huge_alloc.cc
  src/util/tls_registry.cc)

# Transport-specific. Mellanox OFED drivers are the best choice for raw and
# infiniband, but they do not play well with DPDK. So we compile only one
# transport. Other transports are exluded using preprocessor macros.
string(TOUPPER ${TRANSPORT} DEFINE_TRANSPORT)
add_definitions(-D${DEFINE_TRANSPORT}=true)
MESSAGE(STATUS "Selected transport = ${TRANSPORT}.")
SET(CONFIG_IS_ROCE false)

if(TRANSPORT STREQUAL "dpdk")
  find_library(DPDK_LIB dpdk)
  if(NOT DPDK_LIB)
    message(FATAL_ERROR "dpdk library not found")
  endif()

  set(LIBRARIES ${LIBRARIES} dpdk dl)
  SET(CONFIG_TRANSPORT "DpdkTransport")
  SET(CONFIG_HEADROOM 40)

  # DPDK include directory. Locating rte_config.h does not work on some systems.
  # Example: it may be kept in /usr/include/x86_64-linux-gnu/, and symlinked
  # from the real DPDK include directory (/usr/include/dpdk/).
  find_path(DPDK_INCLUDE_DIR NAMES rte_ethdev.h PATH_SUFFIXES dpdk)
  if (DPDK_INCLUDE_DIR)
    message(STATUS "DPDK include directory = ${DPDK_INCLUDE_DIR}")
  else()
    message(FATAL_ERROR "DPDK include directory not found. Please install DPDK.")
  endif()
  include_directories(SYSTEM ${DPDK_INCLUDE_DIR})
else()
  find_library(IBVERBS_LIB ibverbs)
  if(NOT IBVERBS_LIB)
    message(FATAL_ERROR "ibverbs library not found")
  endif()

  set(LIBRARIES ${LIBRARIES} ibverbs)
  if(TRANSPORT STREQUAL "raw")
    SET(CONFIG_TRANSPORT "RawTransport")
    SET(CONFIG_HEADROOM 40)
  elseif(TRANSPORT STREQUAL "infiniband")
    SET(CONFIG_TRANSPORT "IBTransport")
    if(ROCE)
      SET(CONFIG_HEADROOM 40)
      SET(CONFIG_IS_ROCE true)
    else()
      SET(CONFIG_HEADROOM 0)
      SET(CONFIG_IS_ROCE false)
    endif()
  endif()
endif()

configure_file(src/config.h.in src/config.h)

# MICA sources
set(MICA_SOURCES
  mica/src/mica/util/cityhash/city_mod.cc
  mica/src/mica/util/config.cc)

# The tests to run using ctest
set(CLIENT_TESTS
  create_session_test
  destroy_session_test
  small_msg_test
  large_msg_test
  req_in_cont_func_test
  req_in_req_func_test
  packet_loss_test)

set(PROTOCOL_TESTS
  rpc_sm_test
  rpc_list_test
  rpc_req_test
  rpc_resp_test
  rpc_cr_test
  rpc_rfr_test
  rpc_kick_test)

if(TRANSPORT STREQUAL "raw")
  set(TRANSPORT_TESTS
    raw_transport_test)
endif()

# These are not run using ctest
set(UTIL_TESTS
  huge_alloc_test
  timing_wheel_test
  rand_test
  misc_test
  fixed_vector_test
  timely_test
  numautil_test)

# Compile the library
if(COMPILE_ERPC_LIB)
  MESSAGE(STATUS "Compiling eRPC as a library")
  add_library(erpc ${SOURCES})
endif()

# Compile the tests
if(TESTING)
  foreach(test_name IN LISTS CLIENT_TESTS)
    add_executable(${test_name} tests/client_tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${GTEST_LIBRARIES} ${LIBRARIES})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  foreach(test_name IN LISTS PROTOCOL_TESTS)
    add_executable(${test_name} tests/protocol_tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${GTEST_LIBRARIES} ${LIBRARIES})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  foreach(test_name IN LISTS TRANSPORT_TESTS)
    add_executable(${test_name} tests/transport_tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${GTEST_LIBRARIES} ${LIBRARIES})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  foreach(test_name IN LISTS UTIL_TESTS)
    add_executable(${test_name} tests/util_tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${GTEST_LIBRARIES} ${LIBRARIES})
  endforeach()
endif()

# The app to compile. Only one app is compiled to reduce compile time.
if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/autorun_app_file")
  file(STRINGS "scripts/autorun_app_file" APP)
else()
  MESSAGE(STATUS "No autorun_app_file found. No application will be compiled.")
  return()
endif()

message(STATUS "Compiling app = " ${APP})

# Add app-specific defines now, isolating them from the tests

# libpmem is installable from package managers in only recent distros. If it's
# not present, don't link it in.
find_library(PMEM_LIB pmem)
if(NOT PMEM_LIB)
  message(STATUS "pmem library not found")
  SET(PMEM "")
else()
  SET(PMEM "pmem")
endif()

if(APP STREQUAL "smr")
  # Raft library from willemt/raft, installed at system-level
  set(LIBRARIES ${LIBRARIES} raft ${PMEM})
  # MICA
  include_directories(mica/src)
  set(APP_ADDITIONAL_SOURCES ${MICA_SOURCES})
elseif(APP STREQUAL "masstree_analytics")
  # CMake-based Masstree library from anujkaliaiitd/masstree-beta
  link_directories(${CMAKE_SOURCE_DIR}/third_party/masstree-beta)
  include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/third_party/masstree-beta)
  add_definitions(-include ${CMAKE_SOURCE_DIR}/third_party/masstree-beta/config.h)
  set(APP_ADDITIONAL_SOURCES
    apps/masstree_analytics/mt_index_api.cc
    mica/src/mica/util/cityhash/city_mod.cc)
  set(LIBRARIES ${LIBRARIES} masstree)
  include_directories(mica/src)
elseif(APP STREQUAL "mica_test")
  include_directories(mica/src)
  set(APP_ADDITIONAL_SOURCES ${MICA_SOURCES})
elseif(APP STREQUAL "latency")
  set(LIBRARIES ${LIBRARIES} ${PMEM})
elseif(APP STREQUAL "persistent_kv")
  set(LIBRARIES ${LIBRARIES} ${PMEM} cityhash)
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
