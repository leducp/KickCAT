add_executable(kickmsg_bench bench.cc)
target_link_libraries(kickmsg_bench PRIVATE kickmsg kickcat)
set_kickcat_properties(kickmsg_bench)
set_target_properties(kickmsg_bench
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(kickmsg_microbench microbench.cc)
target_link_libraries(kickmsg_microbench PRIVATE kickmsg kickcat benchmark::benchmark)
set_kickcat_properties(kickmsg_microbench)
set_target_properties(kickmsg_microbench
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
