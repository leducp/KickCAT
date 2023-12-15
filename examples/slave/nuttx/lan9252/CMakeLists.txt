project(easycat_slave)

add_executable(main main.cc)
target_link_libraries(main kickcat)
set_kickcat_properties(main)
