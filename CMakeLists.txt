cmake_minimum_required(VERSION 2.8)
project(eRPC)

include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Parse the build type
if(PERF)
  message(STATUS "Compilation optimized for performance.")
  message(STATUS "Debugging is disabled.")
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
  set(TESTING OFF)
else(PERF)
  message(STATUS "Compilation not optimized for performance.")
  message(STATUS "Debugging is enabled. Perf will be low.")
  set(CMAKE_BUILD_TYPE Debug)
  SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")

  # For now, disable unit tests on Windows
  if (NOT WIN32)
    set(TESTING ON)
  endif()
endif(PERF)

# Common sub-projects: HdrHistogram
set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "Minimize HDR histogram build")
set(HDR_LOG_REQUIRED OFF CACHE BOOL "Disable HDR histogram's log to avoid zlib dependency")
add_subdirectory(third_party/HdrHistogram_c)

# Common sub-projects: gtest
set(INSTALL_GTEST OFF)
add_subdirectory(third_party/googletest)
enable_testing()
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

# Common sub-projects: gflags
add_subdirectory(third_party/gflags)

# Common sub-projects: asio
include_directories(SYSTEM third_party/asio/include)

# Add additional compilation flags only after adding subprojects
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -march=native -Wall -Wextra -Werror -pedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-conversion -Wold-style-cast -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nested-anon-types -Wno-keyword-macro -Wno-deprecated-declarations")

include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/third_party)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# DPDK may be needed for different reasons (e.g., the transport is DPDK, or
# if the transport is not DPDK but the application needs DPDK libraries)
set(DPDK_NEEDED "false")

# Options exposed to the user
set(TRANSPORT "dpdk" CACHE STRING "Datapath transport (infiniband/raw/dpdk/fake)")
option(ROCE "Use RoCE if TRANSPORT is infiniband" OFF)
option(AZURE "Configure DPDK for Azure if TRANSPORT is dpdk" OFF)
option(PERF "Compile for performance" ON)
set(PGO "none" CACHE STRING "Profile-guided optimization (generate/use/none)")
set(LOG_LEVEL "warn" CACHE STRING "Logging level (none/error/warn/info/reorder/trace/cc)") 

# Profile-guided optimization
if(PGO STREQUAL "generate")
  message(STATUS "Profile-guided optimization (generate mode) is enabled. Performance will be low.")
  add_definitions(-fprofile-generate)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-generate")
elseif(PGO STREQUAL "use")
  message(STATUS "Profile-guided optimization (use mode) is enabled.")
  add_definitions(-fprofile-use -fprofile-correction)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-use -fprofile-correction")
elseif(PGO STREQUAL "none")
  message(STATUS "Profile-guided optimization is disabled.")
endif()

# Logging level
if(LOG_LEVEL STREQUAL "none")
  message(STATUS "Logging level = none.")
  add_definitions(-DERPC_LOG_LEVEL=0)
elseif(LOG_LEVEL STREQUAL "error")
  message(STATUS "Logging level = error.")
  add_definitions(-DERPC_LOG_LEVEL=1)
elseif(LOG_LEVEL STREQUAL "warn")
  message(STATUS "Logging level = warn.")
  add_definitions(-DERPC_LOG_LEVEL=2)
elseif(LOG_LEVEL STREQUAL "info")
  message(STATUS "Logging level = info.")
  add_definitions(-DERPC_LOG_LEVEL=3)
elseif(LOG_LEVEL STREQUAL "reorder")
  message(STATUS "Logging level = reorder. Warning: Performance will be low.")
  add_definitions(-DERPC_LOG_LEVEL=4)
elseif(LOG_LEVEL STREQUAL "trace")
  message(STATUS "Logging level = trace. Trace, on! Warning: Performance will be low.")
  add_definitions(-DERPC_LOG_LEVEL=5)
elseif(LOG_LEVEL STREQUAL "cc")
  message(STATUS "Logging level = cc. Warning: Performance will be low.")
  add_definitions(-DERPC_LOG_LEVEL=6)
else()
  message(STATUS "No logging level specified. Using warning level.")
  add_definitions(-DERPC_LOG_LEVEL=2)
endif()

# Testing for packet loss, machine failure, etc
if(TESTING)
  message(STATUS "Testing is enabled. Performance will be low.")
  add_definitions(-DERPC_TESTING=true)
else(TESTING)
  message(STATUS "Testing is disabled, so tests may fail.")
  add_definitions(-DERPC_TESTING=false)
endif(TESTING)

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
  src/transport_impl/dpdk/dpdk_init.cc
  src/transport_impl/dpdk/dpdk_externs.cc
  src/transport_impl/dpdk/dpdk_transport_datapath.cc
  src/transport_impl/infiniband/ib_transport.cc
  src/transport_impl/infiniband/ib_transport_datapath.cc
  src/transport_impl/raw/raw_transport.cc
  src/transport_impl/raw/raw_transport_datapath.cc
  src/transport_impl/fake/fake_transport.cc
  src/util/huge_alloc.cc
  src/util/numautils.cc
  src/util/tls_registry.cc)

# Transport-specific. Mellanox OFED drivers are the best choice for raw and
# infiniband, but they do not play well with DPDK. So we compile only one
# transport. Other transports are exluded using preprocessor macros.
string(TOUPPER ${TRANSPORT} DEFINE_TRANSPORT)
add_definitions(-DERPC_${DEFINE_TRANSPORT}=true)
message(STATUS "Selected transport = ${TRANSPORT}.")
set(CONFIG_IS_ROCE false)

if(TRANSPORT STREQUAL "dpdk")
  set(CONFIG_TRANSPORT "DpdkTransport")
  set(CONFIG_HEADROOM 40)
  set(DPDK_NEEDED "true") # We'll resolve DPDK later

  if(AZURE)
    set(CONFIG_IS_AZURE true)
    message(STATUS "Configuring DPDK for Azure")
  else()
    set(CONFIG_IS_AZURE false)
    message(STATUS "Configuring DPDK for bare-metal cluster (i.e., not Azure)")
  endif()
elseif(TRANSPORT STREQUAL "fake")
  set(CONFIG_IS_AZURE false)
  set(CONFIG_TRANSPORT "FakeTransport")
  set(CONFIG_HEADROOM 40)
else()
  set(CONFIG_IS_AZURE false)
  find_library(IBVERBS_LIB ibverbs)
  if(NOT IBVERBS_LIB)
    message(FATAL_ERROR "ibverbs library not found")
  endif()

  set(LIBRARIES ${LIBRARIES} ibverbs)
  if(TRANSPORT STREQUAL "raw")
    set(CONFIG_TRANSPORT "RawTransport")
    set(CONFIG_HEADROOM 40)
    message(FATAL_ERROR
      "eRPC no longer supports Raw transport, please use DTRANSPORT=dpdk instead. "
      "If you wish to use the Raw transport, please use this version: "
      "https://github.com/erpc-io/eRPC/releases/tag/v0.1. You'll need to "
      "install an old version of Mellanox OFED (4.4 or older).")
  elseif(TRANSPORT STREQUAL "fake")
    set(CONFIG_TRANSPORT "FakeTransport")
    set(CONFIG_HEADROOM 40)
  elseif(TRANSPORT STREQUAL "infiniband")
    set(CONFIG_TRANSPORT "IBTransport")
    if(ROCE)
      set(CONFIG_HEADROOM 40)
      set(CONFIG_IS_ROCE true)
    else()
      set(CONFIG_HEADROOM 0)
      set(CONFIG_IS_ROCE false)
    endif()
  endif()
endif()

# Generate config.h
configure_file(src/config.h.in src/config.h)
include_directories(${CMAKE_BINARY_DIR}/src)

# MICA sources
set(MICA_SOURCES
  mica/src/mica/util/cityhash/city_mod.cc
  mica/src/mica/util/config.cc)

# The app to compile. Only one app is compiled to reduce compile time.
if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/autorun_app_file")
  file(STRINGS "scripts/autorun_app_file" APP)
else()
  message(STATUS "No autorun_app_file found. No application will be compiled.")
  return()
endif()
message(STATUS "Compiling app = " ${APP})

# Add app-specific defines now, isolating them from the library and tests

# libpmem is installable from package managers in only recent distros. If it's
# not present, don't link it in.
find_library(PMEM_LIB pmem)
if(NOT PMEM_LIB)
  message(STATUS "Persistent memory library (libpmem) not found")
  set(PMEM "")
else()
  set(PMEM "pmem")
endif()

if(APP STREQUAL "smr")
  include_directories(mica/src)
  add_subdirectory(${CMAKE_SOURCE_DIR}/apps/smr/willemt_raft)
  include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/apps/smr/willemt_raft/include)
  if (WIN32)
    link_directories(${CMAKE_SOURCE_DIR}/build)
    set(LIBRARIES ${LIBRARIES} raft.lib)
  else()
    link_directories(${CMAKE_SOURCE_DIR}/apps/smr/willemt_raft)
    set(LIBRARIES ${LIBRARIES} raft ${PMEM})
  endif()
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
elseif(APP STREQUAL "pmem_bw")
  set(LIBRARIES ${LIBRARIES} ${PMEM})
  message(STATUS "DPDK is needed for pmem_bw application")
  add_definitions(-fpermissive)
  set(DPDK_NEEDED "true")
elseif(APP STREQUAL "persistent_kv")
  set(LIBRARIES ${LIBRARIES} ${PMEM} cityhash)
elseif(APP STREQUAL "log")
  set(LIBRARIES ${LIBRARIES} ${PMEM})
endif()

if(DPDK_NEEDED STREQUAL "true")
  message(STATUS "DPDK needed to build eRPC")

  if (WIN32)
    message(STATUS "DPDK for Windows")

    # Set this to your DPDK directory
    set(DPDK_WINDIR "${CMAKE_CURRENT_SOURCE_DIR}/../dpdk")
    if(NOT EXISTS "${DPDK_WINDIR}/lib/eal/include/rte_common.h")
      message(FATAL_ERROR "rte_common.h not found. DPDK_WINDIR = ${DPDK_WINLIBS}")
      return()
    endif()

    set(DPDK_WINLIBS
      acl bbdev bitratestats bpf cfgfile cmdline compressdev cryptodev
      distributor eal efd ethdev eventdev fib flow_classify graph gro gso hash
      ip_frag ipsec jobstats kni kvargs latencystats lpm mbuf member mempool
      meter metrics net node pci pdump pipeline port power rawdev rcu regexdev
      reorder rib ring sched security stack table telemetry timer vhost)
    foreach(lib_name IN LISTS DPDK_WINLIBS)
      include_directories(SYSTEM "${DPDK_WINDIR}/lib/${lib_name}")
    endforeach()

    include_directories(SYSTEM "${DPDK_WINDIR}/lib/eal/include")
    include_directories(SYSTEM "${DPDK_WINDIR}/lib/eal/windows/include")
    include_directories(SYSTEM "${DPDK_WINDIR}/lib/eal/x86/include")
    include_directories(SYSTEM "${DPDK_WINDIR}/config")
    include_directories(SYSTEM "${DPDK_WINDIR}/build")

    link_directories("${DPDK_WINDIR}/build/lib")
    link_directories("${DPDK_WINDIR}/build/drivers")

    set(LIBRARIES ${LIBRARIES}
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_cfgfile.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_hash.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_cmdline.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_pci.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_ethdev.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_meter.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_net.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_mbuf.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_mempool.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_rcu.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_ring.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_eal.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_telemetry.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/lib/librte_kvargs.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/drivers/librte_common_iavf.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/drivers/librte_bus_pci.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/drivers/librte_bus_vdev.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/drivers/librte_common_mlx5.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/drivers/librte_mempool_ring.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/drivers/librte_net_i40e.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/drivers/librte_net_ice.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/drivers/librte_net_mlx5.a"
      "-Wl,/WHOLEARCHIVE:${DPDK_WINDIR}/build/drivers/librte_net_vmxnet3.a"
      "-lws2_32" "-lmincore" "-ladvapi32" "-lsetupapi" "-ldbghelp"
      "C:/Program Files/Mellanox/MLNX_WinOF2_DevX_SDK/lib/mlx5devx.lib"
      "-Wl,/SUBSYSTEM:CONSOLE" "-lkernel32" "-luser32" "-lgdi32" "-lwinspool"
      "-lshell32" "-lole32" "-loleaut32" "-luuid" "-lcomdlg32" "-liphlpapi")

  else(WIN32)
    message(STATUS "DPDK for Linux")
    find_library(DPDK_LIB dpdk)
    if(NOT DPDK_LIB)
      message(FATAL_ERROR "DPDK library not found")
    endif()

    set(LIBRARIES ${LIBRARIES} -Wl,--whole-archive dpdk -Wl,--no-whole-archive numa dl ibverbs mlx4 mlx5)

    # DPDK include directory. Locating rte_config.h does not work on some systems.
    # Example: it may be kept in /usr/include/x86_64-linux-gnu/, and symlinked
    # from the real DPDK include directory (/usr/include/dpdk/).
    find_path(DPDK_INCLUDE_DIR NAMES rte_ethdev.h PATH_SUFFIXES dpdk)
    if (DPDK_INCLUDE_DIR)
      message(STATUS "DPDK include directory = ${DPDK_INCLUDE_DIR}")
    else()
      message(FATAL_ERROR "DPDK include directory not found")
    endif()
    include_directories(SYSTEM ${DPDK_INCLUDE_DIR})
  endif(WIN32)
else()
  message(STATUS "DPDK not needed to build eRPC")
endif()

set(LIBRARIES ${LIBRARIES} gflags hdr_histogram_static)
if(NOT WIN32)
  set(LIBRARIES ${LIBRARIES} rt numa pthread)
endif()

# Compile the library
add_library(erpc ${SOURCES})

if(DPDK_NEEDED STREQUAL "true")
  if (NOT WIN32)
    # Build the eRPC DPDK management daemon for Linux only
    add_executable(erpc_dpdk_daemon
      src/transport_impl/dpdk/dpdk_daemon.cc
      src/transport_impl/dpdk/dpdk_externs.cc
      src/transport_impl/dpdk/dpdk_init.cc)
    target_link_libraries(erpc_dpdk_daemon ${LIBRARIES})
  endif()
endif()

# Using link-time optimization sometimes requires building with sources instead
# of liberpc. See the hello world example's Makefile for an example of
# compiling with liberpc.
add_executable(${APP} apps/${APP}/${APP}.cc ${APP_ADDITIONAL_SOURCES} ${SOURCES})

if(PERF)
  target_compile_options(${APP} PRIVATE "-flto")
endif()
target_link_libraries(${APP} ${LIBRARIES})

# Compile hello_world apps
add_executable(hello_server hello_world/server.cc)
target_link_libraries(hello_server erpc ${LIBRARIES})
add_executable(hello_client hello_world/client.cc)
target_link_libraries(hello_client erpc ${LIBRARIES})

# Compile the tests
if(TESTING)
  set(LIBRARIES ${LIBRARIES} gtest)

  # End-to-end tests with an RPC server and client
  set(CLIENT_TESTS
    create_session_test
    destroy_session_test
    small_msg_test
    large_msg_test
    req_in_cont_func_test
    req_in_req_func_test
    packet_loss_test
    #server_failure_test
    multi_process_test)
  foreach(test_name IN LISTS CLIENT_TESTS)
    add_executable(${test_name} tests/client_tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${LIBRARIES})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  # Tests for internal eRPC protocol implementation
  set(PROTOCOL_TESTS
    rpc_sm_test
    rpc_list_test
    rpc_req_test
    rpc_resp_test
    rpc_cr_test
    rpc_rfr_test
    rpc_kick_test)
  foreach(test_name IN LISTS PROTOCOL_TESTS)
    add_executable(${test_name} tests/protocol_tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${LIBRARIES})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  # Tests for the transport backend implementation
  if(TRANSPORT STREQUAL "raw")
    set(TRANSPORT_TESTS
      raw_transport_test)
  endif()
  if(TRANSPORT STREQUAL "dpdk")
    set(TRANSPORT_TESTS
      dpdk_ownership_memzone_test)
  endif()

  foreach(test_name IN LISTS TRANSPORT_TESTS)
    add_executable(${test_name} tests/transport_tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${LIBRARIES})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  # Utils that depend on liberpc. These tests are not run using ctest.
  set(UTIL_TESTS_DEP
    huge_alloc_test
    timing_wheel_test)

  foreach(test_name IN LISTS UTIL_TESTS_DEP)
    add_executable(${test_name} tests/util_tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${LIBRARIES})
  endforeach()

  # Utils that don't depend on liberpc. These tests are not run using ctest.
  set(UTIL_TESTS_INDEP
    heartbeat_mgr_test
    rand_test
    misc_test
    fixed_vector_test
    timely_test
    numautil_test
    hdr_histogram_test
    udp_client_test
    udp_server_test)

  # Non-Windows tests
  if (NOT WIN32)
    set(UTIL_TESTS_INDEP ${UTIL_TESTS_INDEP}
      hugepage_caching_virt2phy_test)
  endif()

  foreach(test_name IN LISTS UTIL_TESTS_INDEP)
    add_executable(${test_name} tests/util_tests/${test_name}.cc)
    target_link_libraries(${test_name} erpc ${LIBRARIES})
  endforeach()
endif()
