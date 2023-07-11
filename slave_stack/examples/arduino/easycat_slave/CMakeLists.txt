#cmake_minimum_required(VERSION 3.0.0)
#
#project(easycat_slave CXX)
#
#
#set(STACK_LIB_SOURCES
#    ${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers/arduino/spi.cc
#    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/AbstractSPI.cc
#    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/Error.cc
#    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/Time.cc
#    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/ESC/Lan9252.cc
#   )
#
#add_executable(easycat_slave easycat_slave.cpp ${STACK_LIB_SOURCES})
#
#
#target_include_directories(easycat_slave PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers/
#                                                 ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)
#
## Arduino.h is included in hello_world.cpp, so link with Arduino core
#target_link_arduino_libraries(easycat_slave PRIVATE core SPI)
#
## This is needed for the generation of HEX binary and uploading it
#target_enable_arduino_upload(easycat_slave)

