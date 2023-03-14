if (PIKEOS)
    set(OS_SRCS ${PROJECT_SOURCE_DIR}/src/OS/PikeOS/p4ext_config.c)
endif()

add_executable(elmo_control elmo_control.cc ${OS_SRCS})
target_include_directories(elmo_control PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(elmo_control common kickcat)
set_kickcat_properties(elmo_control)
