set(EXAMPLE_SRCS easycat_example.cc)
if (PIKEOS)
    set(OS_SRCS ${PROJECT_SOURCE_DIR}/src/OS/PikeOS/p4ext_config.c)
endif()

#add_executable(easycat_example ${EXAMPLE_SRCS} ${OS_SRCS})
#target_link_libraries(easycat_example kickcat)
#set_target_properties(easycat_example PROPERTIES
#    CXX_STANDARD 17
#    CXX_STANDARD_REQUIRED YES
#    CXX_EXTENSIONS NO
#    POSITION_INDEPENDENT_CODE ON
#)

set(EXAMPLE_REDUNDANCY_SRCS easycat_redundancy_example.cc)
add_executable(easycat_redundancy_example ${EXAMPLE_REDUNDANCY_SRCS} ${OS_SRCS})
target_link_libraries(easycat_redundancy_example kickcat)
set_target_properties(easycat_redundancy_example PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
