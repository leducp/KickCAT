add_executable(hw_test_bench hw_test_bench.cc)
target_link_libraries(hw_test_bench PRIVATE kickcat argparse::argparse)
install(TARGETS hw_test_bench RUNTIME DESTINATION bin)
