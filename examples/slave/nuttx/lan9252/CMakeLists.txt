project(easycat_slave)

add_executable(easycat_slave main.cc)
target_link_libraries(easycat_slave kickcat)
set_kickcat_properties(easycat_slave)
