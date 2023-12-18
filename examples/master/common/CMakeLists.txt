add_library (common src/CanOpenStateMachine.cc)
target_include_directories(common PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(common kickcat)
set_kickcat_properties(common)
