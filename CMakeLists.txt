cmake_minimum_required(VERSION 2.6)
project(sorter)

# RELEASE
# set (CMAKE_BUILD_TYPE release) 
# set (CMAKE_EXE_LINKER_FLAGS "-s")  ## Strip binary

# DEBUG
set (CMAKE_BUILD_TYPE debug) 
set (CMAKE_EXE_LINKER_FLAGS "")  ## NOT Strip binary

set (SRC ${SRC} sorter.c)
add_executable (sorter ${SRC})
