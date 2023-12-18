add_executable(server server.cc)
target_link_libraries(server kickcat)
set_kickcat_properties(server)

add_executable(emitter emitter.cc)
target_link_libraries(emitter kickcat)
set_kickcat_properties(emitter)
