set(TARGET_NAME easycat_raspberrypi)
add_executable(${TARGET_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/od_populator.cc)
target_link_libraries(${TARGET_NAME} kickcat)
set_kickcat_properties(${TARGET_NAME})
