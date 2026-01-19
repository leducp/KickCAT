set(EXAMPLE_SRCS topology_example.cc)
if (PIKEOS)
    set(OS_SRCS ${PROJECT_SOURCE_DIR}/src/OS/PikeOS/p4ext_config.c)
endif()

add_executable(topology_example ${EXAMPLE_SRCS} ${OS_SRCS})
target_link_libraries(topology_example kickcat)
set_kickcat_properties(topology_example)











