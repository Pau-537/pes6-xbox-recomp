# pes6 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(pes6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(PES6_SOURCES
    src/main.cpp
)

if(WIN32)
    set(PES6_SOURCES ${PES6_SOURCES} resources.rc)
    add_executable(pes6 WIN32 ${PES6_SOURCES})
else()
    add_executable(pes6 ${PES6_SOURCES})
endif()

rexglue_setup_target(pes6)
