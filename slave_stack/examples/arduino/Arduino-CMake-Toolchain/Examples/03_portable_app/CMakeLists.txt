cmake_minimum_required(VERSION 3.0.0)

project(portable_app CXX)

add_subdirectory(abstraction/app_entry)
add_subdirectory(abstraction/app_stdio)

add_executable(portable_app portable_app.cpp)

target_link_libraries(portable_app PRIVATE app_entry app_stdio)

# This Arduino specific statement is required in order to generate HEX
# and upload it to the board. May be removed in future, but currently
# wrapped with ARDUINO flag to ensure script portability.
if (ARDUINO)
	target_enable_arduino_upload(portable_app)
endif()

