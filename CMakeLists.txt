cmake_minimum_required(VERSION 3.22)
project(remotewin CXX)

# A project of its own. It knows how to move windows, pixels and input
# across a boundary, and nothing about virtual machines, libvirt or
# whoever is managing them — so it builds and tests on its own, and a
# caller consumes the executables rather than linking the code.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(RWIN_BUILD_TESTS "Build the test suite" ON)
if(RWIN_BUILD_TESTS)
    enable_testing()
endif()

# The wire format and the vocabulary: header-only, dependency-free, and
# compiled by both toolchains. Everything else is built on top of it.
add_subdirectory(src/proto)
