cmake_minimum_required(VERSION 3.0.0)

project(xmc4800_slave CXX)

add_executable(xmc4800_relax main_relax.cc od_populator.cc)
target_link_libraries(xmc4800_relax kickcat)
set_kickcat_properties(xmc4800_relax)


add_executable(xmc4800_foot main_foot.cc od_populator.cc)
target_link_libraries(xmc4800_foot kickcat)
set_kickcat_properties(xmc4800_foot)
