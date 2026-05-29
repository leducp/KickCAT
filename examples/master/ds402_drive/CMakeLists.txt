if (PIKEOS)
    set(OS_SRCS ${PROJECT_SOURCE_DIR}/src/OS/PikeOS/p4ext_config.c)
endif()

add_executable(ds402_sinus ds402_sinus.cc ${OS_SRCS})
target_link_libraries(ds402_sinus kickcat argparse::argparse)
set_kickcat_properties(ds402_sinus)
