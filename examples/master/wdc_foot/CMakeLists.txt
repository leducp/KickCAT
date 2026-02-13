set(EXAMPLE_SRCS wdc_foot_example.cc)
if (PIKEOS)
    set(OS_SRCS ${PROJECT_SOURCE_DIR}/src/OS/PikeOS/p4ext_config.c)
endif()

add_executable(wdc_foot_example ${EXAMPLE_SRCS} ${OS_SRCS})
target_link_libraries(wdc_foot_example kickcat argparse::argparse)
set_kickcat_properties(wdc_foot_example)
