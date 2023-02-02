if (PIKEOS)
    set(OS_SRCS ${PROJECT_SOURCE_DIR}/src/OS/PikeOS/p4ext_config.c)
endif()

add_executable(elmoControl elmoControl.cc src/CanOpenStateMachine.cc ${OS_SRCS})
target_include_directories(elmoControl PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(elmoControl kickcat)
set_kickcat_properties(elmoControl)
