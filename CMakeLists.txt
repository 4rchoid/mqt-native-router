# SPDX-License-Identifier: Apache-2.0
#
# Modern CMake (>= 3.16), strict C++17, zero-dependency build.

cmake_minimum_required(VERSION 3.16)

project(mqss-routing-heuristic
    VERSION     0.1.0
    DESCRIPTION "MQT-inspired quantum routing heuristic for the MQSS"
    LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Default to Release when no build type / multi-config generator is set.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Debug|Release|RelWithDebInfo|MinSizeRel)" FORCE)
endif()

# --- Core library --------------------------------------------------------

add_library(mqss_router_core STATIC
    src/Architecture.cpp
    src/Router.cpp
)

target_include_directories(mqss_router_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(mqss_router_core PUBLIC cxx_std_17)

# --- Executable ----------------------------------------------------------

add_executable(mqss_router src/main.cpp)
target_link_libraries(mqss_router PRIVATE mqss_router_core)

# --- Warnings & optimization --------------------------------------------

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(MQSS_WARN_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
    )
    target_compile_options(mqss_router_core PRIVATE ${MQSS_WARN_FLAGS})
    target_compile_options(mqss_router      PRIVATE ${MQSS_WARN_FLAGS})

    target_compile_options(mqss_router_core PRIVATE
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    )
    target_compile_options(mqss_router PRIVATE
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    )
elseif(MSVC)
    target_compile_options(mqss_router_core PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(mqss_router      PRIVATE /W4 /permissive- /EHsc)
endif()
