add_executable(scout scout.cc ${OS_SRCS})
target_include_directories(scout PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(scout common kickcat)
set_kickcat_properties(scout)

add_executable(scout_info scout_info.cc ${OS_SRCS})
target_include_directories(scout_info PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(scout_info common kickcat)
set_kickcat_properties(scout_info)
