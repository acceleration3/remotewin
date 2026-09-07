cmake_minimum_required(VERSION 3.22)
project(remotewin CXX)

# A project of its own: it knows how to move windows, pixels and input
# across a boundary, and nothing about virtual machines or whoever is
# managing them.
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

add_subdirectory(log)
add_subdirectory(proto)

# The two halves are each built by the toolchain that can: the guest is a
# Windows executable, the host a Linux one. A native Linux build makes the
# host, a mingw cross build makes the guest, and neither pretends it can
# make the other.
if(WIN32)
    add_subdirectory(guest)
else()
    add_subdirectory(host)
endif()
