cmake_minimum_required(VERSION 3.3.2)
project(wyatt-stm)

if (APPLE OR WIN32)
    set(Boost_USE_STATIC_LIBS TRUE)
endif()
find_package(Boost 1.59 REQUIRED COMPONENTS)

include_directories(wstm STM SYSTEM ${Boost_INCLUDE_DIRS})

#use this when all the files are fixed
#file(GLOB SOURCES "src/*.cpp")

#for now add source files by hand
set(SOURCES src/stm.cpp)

add_library(wstm ${SOURCES})
