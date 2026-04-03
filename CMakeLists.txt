cmake_minimum_required(VERSION 3.20)
project(kickmsg LANGUAGES CXX)

# Platform-specific sources
if(UNIX)
    list(APPEND KICKMSG_PLATFORM_SOURCES src/OS/Unix/SharedMemory.cc)
    if(APPLE)
        list(APPEND KICKMSG_PLATFORM_SOURCES src/OS/Darwin/Futex.cc)
    else()
        list(APPEND KICKMSG_PLATFORM_SOURCES src/OS/Linux/Futex.cc)
    endif()
else()
    message(FATAL_ERROR "KickMsg: unsupported platform")
endif()

add_library(kickmsg STATIC ${KICKMSG_PLATFORM_SOURCES})

target_include_directories(kickmsg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(APPLE)
    target_link_libraries(kickmsg PUBLIC pthread)
else()
    target_link_libraries(kickmsg PUBLIC rt pthread)
endif()

target_compile_features(kickmsg PUBLIC cxx_std_20)

option(KICKMSG_BUILD_TESTS    "Build KickMsg tests"    OFF)
option(KICKMSG_ENABLE_TSAN    "Build TSan stress test"  OFF)
option(KICKMSG_BUILD_EXAMPLES "Build KickMsg examples"  OFF)

if(KICKMSG_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(KICKMSG_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
