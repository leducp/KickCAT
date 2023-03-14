if (PIKEOS)
    set(OS_SRCS ${PROJECT_SOURCE_DIR}/src/OS/PikeOS/p4ext_config.c)
endif()

add_executable(ingenia_control ingenia_control.cc ${OS_SRCS})
target_include_directories(ingenia_control PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ingenia_control common kickcat)
set_kickcat_properties(ingenia_control)
