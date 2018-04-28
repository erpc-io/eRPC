cmake_minimum_required(VERSION 3.5)
project(eRPC)

# Pick a compiler
#set(CMAKE_CXX_COMPILER clang++-6.0)
#set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_COMPILER g++-7)

add_definitions(-std=c++11)

add_definitions(-Wall -Wextra -Werror -pedantic)
add_definitions(-Wsign-conversion)
add_definitions(-Wold-style-cast)

add_definitions(-Wno-unused-function)
add_definitions(-Wno-nested-anon-types)
add_definitions(-Wno-keyword-macro)
add_definitions(-march=native)
add_definitions(-g)

set(LIBRARIES ${LIBRARIES} rt numa ibverbs pthread gflags papi)

# Testing
enable_testing()
find_package(GTest REQUIRED)

include_directories(${GTEST_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/third_party)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# Compile the ERPC library unless we cannot (profile-guided optimization or LTO)
set(COMPILE_ERPC_LIB ON)

# Options exposed to the user
option(PERF "Compile for performance" OFF)
set(TRANSPORT "infiniband" CACHE STRING "Datapath transport (infiniband/roce/raw)")
set(PGO "none" CACHE STRING "Profile-guided optimization (generate/use/none)")
set(LOG_LEVEL "warn" CACHE STRING "Logging level (none/error/warn/info/reorder/trace/cc)") 

# Parse the user-exposed options
if(PERF)
  MESSAGE(STATUS "Compilation optimized for performance.")
  SET(DEBUG OFF)
  SET(LTO ON)
  SET(TESTING OFF)
else(PERF)
  MESSAGE(STATUS "Compilation not optimized for performance.")
  SET(DEBUG ON)
  SET(LTO OFF)
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
  add_definitions(-g)
else(DEBUG)
  MESSAGE(STATUS "Debugging is disabled.")
  add_definitions(-DNDEBUG)
  add_definitions(-O2)
endif(DEBUG)

# Testing for packet loss, machine failure, etc
if(TESTING)
  MESSAGE(STATUS "Testing is enabled. Performance will be low.")
  add_definitions(-DTESTING)
else(TESTING)
  MESSAGE(STATUS "Testing is disabled, so tests may fail.")
endif(TESTING)

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
  src/transport_impl/infiniband/ib_transport.cc
  src/transport_impl/infiniband/ib_transport_datapath.cc
  src/transport_impl/raw/raw_transport.cc
  src/transport_impl/raw/raw_transport_datapath.cc
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
set(CLIENT_TESTS
  create_session_test
  destroy_session_test
  api_restrictions_test
  small_msg_test
  large_msg_test
  req_in_cont_func_test
  req_in_req_func_test
  packet_loss_test)

set(PROTOCOL_TESTS
  rpc_sm_test
  rpc_req_test
  rpc_resp_test
  rpc_cr_test
  rpc_rfr_test
  rpc_kick_test)

set(TRANSPORT_TESTS
  raw_transport_test)

# These are not run using ctest
set(UTIL_TESTS
  huge_alloc_test
  timing_wheel_test
  rand_test
  fixed_vector_test
  timely_test
  numautil_test)

# Compile the tests
if(COMPILE_ERPC_LIB)
  MESSAGE(STATUS "Compiling eRPC as a library")
  add_library(erpc ${SOURCES})

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
file(STRINGS "scripts/autorun_app_file" APP)
message(STATUS "Compiling app = " ${APP})

# Add app-specific defines now, isolating them from the tests
if(APP STREQUAL "smr")
  # Raft library from willemt/raft, installed at system-level
  set(LIBRARIES ${LIBRARIES} craft)
  set(APP_ADDITIONAL_SOURCES third_party/cityhash/city_mod.cc)
  # MICA
  include_directories(mica/src)
  set(APP_ADDITIONAL_SOURCES ${MICA_SOURCES})
elseif(APP STREQUAL "masstree_analytics")
  # CMake-based Masstree library from anujkaliaiitd/masstree-beta
  link_directories($ENV{HOME}/masstree-beta)
  include_directories(SYSTEM $ENV{HOME}/masstree-beta)
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
