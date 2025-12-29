find_package(real-time-monitor)

add_executable(marvin marvin.cc)
target_include_directories(marvin PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(marvin common kickcat real-time-monitor::real-time-monitor)
set_kickcat_properties(marvin)
