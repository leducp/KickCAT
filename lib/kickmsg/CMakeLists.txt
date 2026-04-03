add_library(kickmsg STATIC
    src/types.cc
)

target_include_directories(kickmsg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# kickcat provides Futex and SharedMemory implementations
target_include_directories(kickmsg PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

set_kickcat_properties(kickmsg)

if(BUILD_UNIT_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_MASTER_EXAMPLES)
    add_subdirectory(examples)
endif()
