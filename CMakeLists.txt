cmake_minimum_required(VERSION 3.22)
project(remotewin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(RWIN_BUILD_TESTS "Build the test suite" ON)

set(RWIN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_subdirectory(source)

if(RWIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
