add_library(kickcat_lua SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/helpers.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/link.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/bus.cc
    )

target_link_libraries(kickcat_lua lua::lua kickcat)

set_target_properties(kickcat_lua PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_NAME kickcat
    PREFIX ""
    )
