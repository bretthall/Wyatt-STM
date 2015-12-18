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

if (APPLE OR WIN32)
  set(Boost_USE_STATIC_LIBS ON)
endif()
find_package(Boost 1.57 REQUIRED COMPONENTS thread program_options system unit_test_framework timer chrono date_time)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    set(warnings "-Wall -Wextra -Werror")
    set_property(GLOBAL PROPERTY CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
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

include_directories(wstm SYSTEM ${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

set(WSTM_SOURCES
  src/stm.cpp
  src/channel.cpp
  src/deferred_result.cpp
  src/persistent_list.cpp
  src/exception.cpp
  src/exception_capture.cpp)

add_library(wstm ${WSTM_SOURCES})
set_property(TARGET wstm PROPERTY CXX_STANDARD 14)

set(UNIT_TEST_SOURCES
  testing/unit-tests/main.cpp
  testing/unit-tests/stm_test.cpp
  testing/unit-tests/channel_tests.cpp
  testing/unit-tests/persistent_list_tests.cpp
  testing/unit-tests/deferred_result_tests.cpp
  testing/unit-tests/exception_capture_tests.cpp)

add_executable(unit_tests ${UNIT_TEST_SOURCES})
set_property(TARGET unit_tests PROPERTY CXX_STANDARD 14)
target_link_libraries(unit_tests wstm ${pthread_lib} ${clang_stdlib_lib} ${Boost_LIBRARIES})

set(CONTENTION_TEST_SOURCES testing/contention/contention_test.cpp)
add_executable(contention_tests ${CONTENTION_TEST_SOURCES})
set_property(TARGET contention_tests PROPERTY CXX_STANDARD 14)
target_link_libraries(contention_tests wstm ${pthread_lib} ${clang_stdlib_lib} ${Boost_LIBRARIES})

set(CHANNEL_TEST_SOURCES testing/channel/channel_test.cpp)
add_executable(channel_tests ${CHANNEL_TEST_SOURCES})
set_property(TARGET channel_tests PROPERTY CXX_STANDARD 14)
target_link_libraries(channel_tests wstm ${pthread_lib} ${clang_stdlib_lib} ${Boost_LIBRARIES})

set(CORRECTNESS_TEST_SOURCES testing/correctness/correctness_test.cpp)
add_executable(correctness_tests ${CORRECTNESS_TEST_SOURCES})
set_property(TARGET correctness_tests PROPERTY CXX_STANDARD 14)
target_link_libraries(correctness_tests wstm ${pthread_lib} ${clang_stdlib_lib} ${Boost_LIBRARIES})
