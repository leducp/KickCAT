cmake_minimum_required(VERSION 3.0.0)

project(auto_link CXX)

# local_lib is present in ../02_arduino_lib and not here. If this
# CMakeLists.txt is used as the main project make file, then the
# following statement will help to find the local_lib.
list(APPEND ARDUINO_LIBRARIES_SEARCH_PATHS_EXTRA
	"${CMAKE_CURRENT_SOURCE_DIR}/../02_arduino_lib")

#=================================================================
# auto_link_app

add_executable(auto_link_app auto_link_app.cpp)

# The below statement should detect EEPROM, Stepper, core and local_lib
# library dependencies automatically and link with auto_link_app
target_link_arduino_libraries(auto_link_app AUTO_PRIVATE)

# This is needed for generation of HEX image and uploading it
target_enable_arduino_upload(auto_link_app)

#=================================================================
# auto_link_lib

add_library(auto_link_lib auto_link_lib.h auto_link_lib.cpp)

# The below statement should detect EEPROM, Stepper, core and local_lib
# library dependencies automatically and linked privately with
# auto_link_lib.
#
# As auto_link_lib.h includes Arduino.h, core is additionally linked
# with INTERFACE linking, so that dependents can include auto_link_lib.h
# without any error.
target_link_arduino_libraries(auto_link_lib
	AUTO_INTERFACE auto_link_lib.h
	AUTO_PRIVATE
)

#=================================================================
# auto_link_custom

add_executable(auto_link_custom auto_link_custom.cpp)

# add a customized core library
add_custom_arduino_core(auto_link_custom_core)

# Link auto_link_custom with custom core
target_link_libraries(auto_link_custom PRIVATE auto_link_custom_core)

# The below statement should detect EEPROM, SD, core and local_lib
# library dependencies automatically and link with auto_link_custom.
# All these libraries link with auto_link_custom_core and not the
# internally maintained target of the core.
target_link_arduino_libraries(auto_link_custom AUTO_PRIVATE)

# This is needed for generation of HEX image and uploading it
target_enable_arduino_upload(auto_link_custom)
