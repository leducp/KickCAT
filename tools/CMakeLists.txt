set(EXAMPLE_TOPO scanTopology.cc)

add_executable(scanTopology ${EXAMPLE_TOPO})
target_link_libraries(scanTopology kickcat)
set_target_properties(scanTopology PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)

set(EXAMPLE_DEBUG debugExample.cc)

add_executable(debugExample ${EXAMPLE_DEBUG})
target_link_libraries(debugExample kickcat)
set_target_properties(debugExample PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
