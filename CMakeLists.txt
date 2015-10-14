cmake_minimum_required(VERSION 3.3.2)
project(wyatt-stm)

if (APPLE OR WIN32)
    set(Boost_USE_STATIC_LIBS TRUE)
endif()
find_package(Boost 1.59 REQUIRED COMPONENTS)

include_directories(STM SYSTEM ${Boost_INCLUDE_DIRS})

#use this when all the files are fixed
#file(GLOB SOURCES "STM/*.cpp")

#for now add source files by hand
set(SOURCES STM/stm.cpp)

add_library(wstm ${SOURCES})
