cmake_minimum_required(VERSION 3.3.2)
project(wyatt-stm)

if (APPLE OR WIN32)
  set(Boost_USE_STATIC_LIBS ON)
endif()
find_package(Boost 1.57 REQUIRED COMPONENTS)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    set(warnings "-Wall -Wextra -Werror")
    set_property(GLOBAL PROPERTY CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
       add_definitions(-DAPPLE_CLANG)
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
if (NOT CONFIGURED_ONCE)
    set(CMAKE_CXX_FLAGS "${warnings}"
        CACHE STRING "Flags used by the compiler during all build types." FORCE)
    set(CMAKE_C_FLAGS   "${warnings}"
        CACHE STRING "Flags used by the compiler during all build types." FORCE)
endif()

include_directories(wstm SYSTEM ${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

#use this when all the files are fixed
#file(GLOB WSTM_SOURCES "src/*.cpp")

#for now add source files by hand
set(WSTM_SOURCES src/stm.cpp)

add_library(wstm ${WSTM_SOURCES})
set_property(TARGET wstm PROPERTY CXX_STANDARD 14)

#use this when all the files are fixed
#file(GLOB UNIT_TEST_SOURCES "Testing/unit-tests/*.cpp")

#for now add source files by hand
set(UNIT_TEST_SOURCES Testing/unit-tests/main.cpp Testing/unit-tests/stm_test.cpp)

add_executable(unit_tests ${UNIT_TEST_SOURCES})
set_property(TARGET unit_tests PROPERTY CXX_STANDARD 14)
target_link_libraries(unit_tests wstm)
