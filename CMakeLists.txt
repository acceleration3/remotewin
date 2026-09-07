cmake_minimum_required(VERSION 3.22)
project(remotewin CXX)

# A project of its own: it knows how to move windows, pixels, input and
# cursors across a boundary, and nothing about virtual machines or
# whoever is managing them.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(RWIN_BUILD_TESTS "Build the test suite" ON)

# Headers live under include/, sources under source/, both organised by
# the same subject folders. One include root, so every header says where
# it belongs: #include "guest/input/pointer_injector.hpp".
set(RWIN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_subdirectory(source)

if(RWIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
