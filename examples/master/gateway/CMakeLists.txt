add_executable(server server.cc)
target_link_libraries(server kickcat argparse::argparse)
set_kickcat_properties(server)

add_executable(emitter emitter.cc)
target_link_libraries(emitter kickcat argparse::argparse)
set_kickcat_properties(emitter)
