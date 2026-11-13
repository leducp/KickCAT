add_executable(hw_test_bench hw_test_bench.cc)
target_link_libraries(hw_test_bench PRIVATE kickcat argparse::argparse)
target_compile_features(hw_test_bench PRIVATE cxx_std_17)
install(TARGETS hw_test_bench RUNTIME DESTINATION bin)

add_executable(esi_validate esi_validate.cc)
target_link_libraries(esi_validate PRIVATE kickcat argparse::argparse)
target_compile_features(esi_validate PRIVATE cxx_std_17)

add_executable(esi_boot esi_boot.cc)
target_link_libraries(esi_boot PRIVATE kickcat argparse::argparse)
target_compile_features(esi_boot PRIVATE cxx_std_17)
