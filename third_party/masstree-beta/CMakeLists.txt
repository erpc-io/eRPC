project(masstree)
cmake_minimum_required(VERSION 3.2)

set(CMAKE_CXX_STANDARD 11)
add_definitions(-include config.h)

set(CMAKE_REQUIRED_DEFINITIONS -pthread)
set(CMAKE_CXX_FLAGS "-std=c++11 -g -DNDEBUG -O3")

INCLUDE(CheckIncludeFiles)
INCLUDE(CheckIncludeFileCXX)
INCLUDE(CheckFunctionExists)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckCXXSymbolExists)
INCLUDE(CheckCSourceCompiles)
INCLUDE(CheckCXXSourceCompiles)
INCLUDE(CheckTypeSize)
INCLUDE(WriteCompilerDetectionHeader)

check_type_size(off_t SIZEOF_OFF_T)
check_type_size(long SIZEOF_LONG)
check_type_size("long long" SIZEOF_LONG_LONG)
check_type_size(size_t SIZEOF_SIZE_T)
check_type_size(int64_t SIZEOF_INT64_T)

if (NOT ${SIZEOF_OFF_T} EQUAL "")
   set(HAVE_OFF_T 1)
endif()

if (NOT ${SIZEOF_LONG_LONG} EQUAL "")
   set(HAVE_LONG_LONG 1)
endif()

if (NOT ${SIZEOF_LONG} EQUAL "")
   set(HAVE_LONG 1)
endif()

if (NOT ${SIZEOF_SIZE_T} EQUAL "")
   set(HAVE_SIZE_T 1)
endif()

if (NOT ${SIZEOF_INT64_T} EQUAL "")
   set(HAVE_INT64_T  1)
endif()

if (${SIZEOF_OFF_T} EQUAL ${SIZEOF_LONG_LONG})
   set(HAVE_OFF_T_IS_LONG_LONG 1)
endif()

if (${SIZEOF_SIZE_T} EQUAL ${SIZEOF_LONG})
   set(HAVE_SIZE_T_IS_UNSIGNED_LONG 1)
endif()

if (${SIZEOF_SIZE_T} EQUAL ${SIZEOF_LONG_LONG})
   set(HAVE_SIZE_T_IS_UNSIGNED_LONG_LONG 1)
endif()

if (${SIZEOF_INT64_T} EQUAL ${SIZEOF_LONG_LONG})
   set(HAVE_INT64_T_IS_LONG_LONG, 1)
endif()

if (${SIZEOF_INT64_T} EQUAL ${SIZEOF_LONG})
   set(HAVE_INT64_T_IS_LONG, 1)
endif()

check_include_files(execinfo.h HAVE_EXECINFO_H)
check_function_exists(clock_gettime HAVE_CLOCK_GETTIME)
check_include_files(inttypes.h HAVE_INTTYPES_H)
check_library_exists(numa numa_num_configured_nodes "" HAVE_LIBNUMA)
check_include_files(memory.h HAVE_MEMORY_H)
check_include_files(numa.h HAVE_NUMA_H)
check_include_file_cxx(functional HAVE_STD_HASH)
check_include_file_cxx(type_traits HAVE_TYPE_TRAITS)
check_cxx_symbol_exists(std::is_trivially_destructible<int>::value type_traits HAVE_STD_IS_TRIVIALLY_DESTRUCTIBLE)
check_cxx_symbol_exists(std::is_trivially_copyable<int>::value type_traits HAVE_STD_IS_TRIVIALLY_COPYABLE)
check_include_files(sys/epoll.h HAVE_EPOLL_H)
check_symbol_exists(vsnprintf stdio.h HAVE_VSNPRINTF)
check_cxx_source_compiles("int f(int &) { return 1; } int f(int &&) { return 0; } int main() { return f(int()); }" HAVE_CXX_RVALUE_REFERENCES)

function(check_compiler_instrinsic INST ARG VARIABLE)
   check_c_source_compiles("int main() { ${INST}(${ARG}); }" ${VARIABLE})
endfunction(check_compiler_instrinsic)

check_compiler_instrinsic(__sync_val_compare_and_swap "(int*) 0, 0, 0" HAVE___SYNC_VAL_COMPARE_AND_SWAP)
check_compiler_instrinsic(__sync_bool_compare_and_swap "(int*) 0, 0, 0" HAVE___SYNC_BOOL_COMPARE_AND_SWAP)
check_compiler_instrinsic(__sync_val_compare_and_swap_8 "(int*) 0, 0, 0" HAVE___SYNC_VAL_COMPARE_AND_SWAP_8)
check_compiler_instrinsic(__sync_fetch_and_add_8 "(int*) 0, 0, 0" HAVE___SYNC_FETCH_AND_ADD_8)
check_compiler_instrinsic(__sync_fetch_and_or_8 "(int*) 0, 0, 0" HAVE___SYNC_FETCH_AND_OR_8)
check_compiler_instrinsic(__builtin_clz "1" HAVE___BUILTIN_CLZ)
check_compiler_instrinsic(__builtin_clzl "1" HAVE___BUILTIN_CLZL)
check_compiler_instrinsic(__builtin_clzll "1" HAVE___BUILTIN_CLZLL)
check_compiler_instrinsic(__builtin_ctz "1" HAVE___BUILTIN_CTZ)
check_compiler_instrinsic(__builtin_ctzl "1" HAVE___BUILTIN_CTZL)
check_compiler_instrinsic(__builtin_ctzll "1" HAVE___BUILTIN_CTZLL)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
write_compiler_detection_header(
        FILE "${CMAKE_CURRENT_BINARY_DIR}/compiler_features.h"
        PREFIX CXX
        COMPILERS GNU Clang
        FEATURES cxx_alias_templates cxx_static_assert cxx_constexpr
)
        
add_library(masstree query_masstree.cc kvthread.cc str.cc string.cc straccum.cc memdebug.cc json.cc compiler.cc)

#install(TARGETS masstree LIBRARY DESTINATION lib)
