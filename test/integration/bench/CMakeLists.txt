add_executable(hw_test_bench hw_test_bench.cc)
target_link_libraries(hw_test_bench PRIVATE kickcat argparse::argparse)
install(TARGETS hw_test_bench RUNTIME DESTINATION bin)

add_executable(esi_validate esi_validate.cc)
target_link_libraries(esi_validate PRIVATE kickcat)

add_executable(esi_boot esi_boot.cc)
target_link_libraries(esi_boot PRIVATE kickcat argparse::argparse)
