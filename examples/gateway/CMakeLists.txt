set(EXAMPLE_SRCS server.cc)

add_executable(server ${EXAMPLE_SRCS})
target_link_libraries(server kickcat)
set_kickcat_properties(server)

add_executable(emitter emitter.cc)
target_link_libraries(emitter kickcat)
set_kickcat_properties(emitter)
