cmake_minimum_required(VERSION 3.0.0)

project(xmc4800_slave CXX)

add_executable(xmc4800_slave main.cc EmulatedEEPROM.cc EEPROM_factory.cc)
target_link_libraries(xmc4800_slave kickcat)
set_kickcat_properties(xmc4800_slave)
