cmake_minimum_required(VERSION 3.0.0)

project(arduino_lib CXX)

add_executable(arduino_lib arduino_lib.cpp)

# Link with EEPROM native library, because arduino_lib.cpp includes EEPROM.h
# Link with Stepper library, because arduino_lib.cpp includes Stepper.h
# Also link with core, because arduino_lib.cpp includes Arduino.h
target_link_arduino_libraries(arduino_lib PRIVATE EEPROM Stepper core)

# arduino_lib.cpp includes local_lib.h, which is an Arduino library present
# inside the project libraries folder.
target_link_arduino_libraries(arduino_lib PRIVATE local_lib)

# This is needed for generation of HEX image and uploading it
target_enable_arduino_upload(arduino_lib)
