cmake_minimum_required(VERSION 3.15)
project(kickcat_find_package_consumer CXX)

find_package(KickCAT CONFIG REQUIRED)

add_executable(consumer ../main.cc)
target_link_libraries(consumer PRIVATE KickCAT::kickcat)
set_target_properties(consumer PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
