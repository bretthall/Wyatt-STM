#Copyright (c) 2015, Wyatt Technology Corporation
#All rights reserved.
#
#Redistribution and use in source and binary forms, with or without
#modification, are permitted provided that the following conditions are
#met:
#
#1. Redistributions of source code must retain the above copyright
#notice, this list of conditions and the following disclaimer.
#
#2. Redistributions in binary form must reproduce the above copyright
#notice, this list of conditions and the following disclaimer in the
#documentation and/or other materials provided with the distribution.
#
#3. Neither the name of the copyright holder nor the names of its
#contributors may be used to endorse or promote products derived from
#this software without specific prior written permission.
#
#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.3.2)
project(wyatt-stm)

set(wstm_version_major 1)
set(wstm_version_minor 0)
set(wstm_version_patch 0)
set(wstm_version_flags "-DWSTM_VERSION_MAJOR=${wstm_version_major} -DWSTM_VERSION_MINOR=${wstm_version_minor} -DWSTM_VERSION_PATCH=${wstm_version_patch}")

if(WSTM_CONFLICT_PROFILING)
  if (NOT DEFINED SQLITE_INCLUDE_DIR)
    message(SEND_ERROR "SQLITE_INCLUDE_DIR must be defined on the command line")
  endif()
  if (NOT DEFINED SQLITE_LIB_DIR)
    message(SEND_ERROR "SQLITE_LIB_DIR must be defined on the command line")
  endif()
endif()

if (APPLE OR WIN32)
  set(Boost_USE_STATIC_LIBS ON)
endif()
find_package(Boost 1.57 REQUIRED COMPONENTS thread program_options system unit_test_framework timer chrono date_time filesystem)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    set(warnings "-Wall -Wextra -Werror")
    set_property(GLOBAL PROPERTY CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
    set(SQLITE_LIB "sqlite3.so")
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
       add_definitions(-DAPPLE_CLANG -DNO_THREAD_LOCAL)
    endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(warnings "/W4 /wd4512 /WX /EHsc")
        # Disabled Warnings:
        #   4512 "assignment operator could not be generated"
        #        This warning provides no useful information and will occur in
        #        well formed programs.
        #        <http://msdn.microsoft.com/en-us/library/hsyx7kbz.aspx>
     if (NOT DEFINED MSVC_MBCS)
        add_definitions(-DUNICODE -D_UNICODE)
     endif()
     add_definitions(-DWIN32)
     set(SQLITE_LIB "sqlite3.dll")
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  set(clang_stdlib_flag "-stdlib=libc++")
  set(clang_stdlib_lib "c++abi")
  set(pthread_lib "pthread")
  add_definitions(-DNON_APPLE_CLANG)
  add_definitions(-DBOOST_TEST_DYN_LINK)
endif()

if (NOT CONFIGURED_ONCE)
    set(CMAKE_CXX_FLAGS "${warnings} ${wstm_version_flags} ${clang_stdlib_flag}"
        CACHE STRING "Flags used by the compiler during all build types." FORCE)
    set(CMAKE_C_FLAGS   "${warnings} ${wstm_version_flags}"
        CACHE STRING "Flags used by the compiler during all build types." FORCE)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(pthread_lib "pthread")
  add_definitions(-DBOOST_TEST_DYN_LINK)
endif()

if(WSTM_CONFLICT_PROFILING)
  message (STATUS "Building with conflict profiling turned on!!!!")
  add_definitions(-DWSTM_CONFLICT_PROFILING)  
endif()

include_directories(wstm SYSTEM ${SQLITE_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})
link_directories(${SQLITE_LIB_DIR} ${Boost_LIBRARY_DIRS})

set(WSTM_HEADERS
  wstm/channel.h
  wstm/deferred_result.h
  wstm/exception.h
  wstm/exception_capture.h
  wstm/exports.h
  wstm/find_arg.h
  wstm/maybe_unused.h
  wstm/persistent_list.h
  wstm/stm.h
  src/on_transaction_end.h)

source_group("Header Files" REGULAR_EXPRESSION ".*\\.h")

set(WSTM_SOURCES
  src/stm.cpp
  src/channel.cpp
  src/deferred_result.cpp
  src/persistent_list.cpp
  src/exception.cpp
  src/exception_capture.cpp)

add_library(wstm ${WSTM_HEADERS} ${WSTM_SOURCES})
set_property(TARGET wstm PROPERTY CXX_STANDARD 14)

if(WSTM_CONFLICT_PROFILING)
  # The conflict profiling stuff goes in its own library so that the
  # conflict_profiling_processor can use it without brining in all the
  # other STM stuff. If we don't do this the processor will generate
  # its own profiling data files which is unescessary and confusing
  # for the user.
  set(WSTM_CP_HEADERS
    wstm/conflict_profiling_processing.h
    src/conflict_profiling_internal.h)
  set(WSTM_CP_SOURCES src/conflict_profiling_internal.cpp)
  add_library(wstm_cp ${WSTM_CP_HEADERS} ${WSTM_CP_SOURCES})
  set_property(TARGET wstm_cp PROPERTY CXX_STANDARD 14)
  set (wstm_cp_lib "wstm_cp")
endif()

set(UNIT_TEST_SOURCES
  testing/unit-tests/main.cpp
  testing/unit-tests/stm_test.cpp
  testing/unit-tests/channel_tests.cpp
  testing/unit-tests/persistent_list_tests.cpp
  testing/unit-tests/deferred_result_tests.cpp
  testing/unit-tests/exception_capture_tests.cpp)

add_executable(unit_tests ${UNIT_TEST_SOURCES})
set_property(TARGET unit_tests PROPERTY CXX_STANDARD 14)
target_link_libraries(unit_tests wstm ${wstm_cp_lib} ${pthread_lib} ${clang_stdlib_lib} ${Boost_LIBRARIES})

set(CONTENTION_TEST_SOURCES testing/contention/contention_test.cpp)
add_executable(contention_tests ${CONTENTION_TEST_SOURCES})
set_property(TARGET contention_tests PROPERTY CXX_STANDARD 14)
target_link_libraries(contention_tests wstm ${wstm_cp_lib} ${pthread_lib} ${clang_stdlib_lib} ${Boost_LIBRARIES})

set(CHANNEL_TEST_SOURCES testing/channel/channel_test.cpp)
add_executable(channel_tests ${CHANNEL_TEST_SOURCES})
set_property(TARGET channel_tests PROPERTY CXX_STANDARD 14)
target_link_libraries(channel_tests wstm ${wstm_cp_lib} ${pthread_lib} ${clang_stdlib_lib} ${Boost_LIBRARIES})

set(CORRECTNESS_TEST_SOURCES testing/correctness/correctness_test.cpp)
add_executable(correctness_tests ${CORRECTNESS_TEST_SOURCES})
set_property(TARGET correctness_tests PROPERTY CXX_STANDARD 14)
target_link_libraries(correctness_tests wstm ${wstm_cp_lib} ${pthread_lib} ${clang_stdlib_lib} ${Boost_LIBRARIES})

if(WSTM_CONFLICT_PROFILING)
  set(CP_PROCESSING_SOURCES util/conflict_profiling_processor/conflict_profiling_processor.cpp)
  add_executable(conflict_profiling_processor ${CP_PROCESSING_SOURCES})
  set_property(TARGET conflict_profiling_processor PROPERTY CXX_STANDARD 14)
  target_link_libraries(conflict_profiling_processor ${wstm_cp_lib} ${SQLITE_LIB} ${pthread_lib} ${clang_stdlib_lib} ${Boost_LIBRARIES})
endif()
