cmake_minimum_required(VERSION 3.0.0)

project(multi_app CXX)

#=================================================================
# APP1

add_executable(app1 app1.cpp)

# Link app1 with Wire native library, and also core
target_link_arduino_libraries(app1 PRIVATE Wire core)

# Needed for the generation of HEX binary and uploading it
target_enable_arduino_upload(app1)

#=================================================================
# APP2

add_executable(app2 app2.cpp)

# Link app2 with Wire native library, but it requires a custom
# value of TWI_BUFFER_LENGTH. So instead of utlizing the default
# Wire library, we add a new Wire library with customized option.
add_custom_arduino_library(app2_wire Wire)
target_compile_definitions(app2_wire PRIVATE -DTWI_BUFFER_LENGTH=128)
target_link_arduino_libraries(app2_wire PUBLIC core)

# Link app2 with the customized Wire native library
target_link_libraries(app2 PRIVATE app2_wire)

# Arduino.h is included in app2, so link with core
target_link_arduino_libraries(app2 PRIVATE core)

# Needed for the generation of HEX binary and uploading it
target_enable_arduino_upload(app2)

