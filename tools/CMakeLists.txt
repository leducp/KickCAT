set(EXAMPLE_SRCS scanTopology.cc)

add_executable(scanTopology ${EXAMPLE_SRCS})
target_link_libraries(scanTopology kickcat)
set_target_properties(scanTopology PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
