add_executable(network_simulator network_simulator.cc)
set_kickcat_properties(network_simulator)
target_link_libraries(network_simulator kickcat)
