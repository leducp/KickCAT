set(EXAMPLE_SRCS server.cc)

add_executable(server ${EXAMPLE_SRCS})
target_link_libraries(server kickcat)
set_target_properties(server PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)

add_executable(emitter emitter.cc)
target_link_libraries(emitter kickcat)
set_target_properties(emitter PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
