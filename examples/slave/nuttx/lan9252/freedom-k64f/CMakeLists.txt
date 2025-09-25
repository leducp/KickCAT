set(TARGET_NAME easycat_frdm_k64f)
add_executable(${TARGET_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/od_populator.cc
    ${CMAKE_SOURCE_DIR}/examples/slave/common/xpg_strerror_r.cc)
target_link_libraries(${TARGET_NAME} kickcat)
set_kickcat_properties(${TARGET_NAME})

add_custom_command(
        TARGET ${TARGET_NAME}
        POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} ARGS -S -O binary ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME} ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin)
