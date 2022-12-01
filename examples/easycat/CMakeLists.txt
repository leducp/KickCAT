set(EXAMPLE_SRCS easycat_example.cc)
if (PIKEOS)
    set(OS_SRCS ${PROJECT_SOURCE_DIR}/src/OS/PikeOS/p4ext_config.c)
endif()

add_executable(easycat_example ${EXAMPLE_SRCS} ${OS_SRCS})
target_link_libraries(easycat_example kickcat)
set_target_properties(easycat_example PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    COMPILE_FLAGS ${WARNINGS_FLAGS}
)
