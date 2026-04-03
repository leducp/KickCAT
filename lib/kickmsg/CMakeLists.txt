cmake_minimum_required(VERSION 3.20)
project(kickmsg LANGUAGES CXX)

# kickmsg is header-only: OS layer (Futex, SharedMemory) is provided by kickcat.
add_library(kickmsg INTERFACE)

target_include_directories(kickmsg INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# kickcat provides Futex and SharedMemory implementations
target_include_directories(kickmsg INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_features(kickmsg INTERFACE cxx_std_17)

option(KICKMSG_BUILD_TESTS    "Build KickMsg tests"    OFF)
option(KICKMSG_BUILD_EXAMPLES "Build KickMsg examples"  OFF)

if(KICKMSG_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(KICKMSG_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
