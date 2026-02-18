add_executable(scanTopology scanTopology.cc)
target_link_libraries(scanTopology kickcat argparse::argparse)
set_kickcat_properties(scanTopology)

add_executable(debugUseCase debugUseCase.cc)
target_link_libraries(debugUseCase kickcat argparse::argparse)
set_kickcat_properties(debugUseCase)

add_executable(eeprom eeprom.cc)
target_link_libraries(eeprom kickcat argparse::argparse)
set_kickcat_properties(eeprom)

if (ENABLE_ESI_PARSER)
    add_executable(od_generator od_generator.cc)
    target_link_libraries(od_generator kickcat argparse::argparse)
endif()


if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_executable(check_network_stability checkNetworkStability.cc)
    target_link_libraries(check_network_stability kickcat argparse::argparse)
    set_kickcat_properties(check_network_stability)
endif()
