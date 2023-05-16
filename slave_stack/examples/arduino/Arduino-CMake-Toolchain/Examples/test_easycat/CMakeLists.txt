cmake_minimum_required(VERSION 3.0.0)

project(easycat CXX)

add_executable(easycat test_easycat.cpp)

# Arduino.h is included in hello_world.cpp, so link with Arduino core
target_link_arduino_libraries(easycat PRIVATE core SPI)

# This is needed for the generation of HEX binary and uploading it
target_enable_arduino_upload(easycat)

