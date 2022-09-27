set(EXAMPLE_SRCS scanTopology.cc)

add_executable(scanTopology ${EXAMPLE_SRCS})
target_link_libraries(scanTopology kickcat)
set_target_properties(scanTopology PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)

set(EXAMPLE_SRCS ./errorsTestbench/errorsTestbench.cc)

add_executable(errorsTestbench ${EXAMPLE_SRCS})
target_link_libraries(errorsTestbench kickcat)
set_target_properties(errorsTestbench PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)