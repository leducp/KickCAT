set(EXAMPLE_SRCS server.cc)

add_executable(server ${EXAMPLE_SRCS})
target_link_libraries(server kickcat)
set_kickcat_properties(server)

add_executable(emitter emitter.cc)
target_link_libraries(emitter kickcat)
set_kickcat_properties(emitter)

add_executable(yolo vsock.cc)
set_kickcat_properties(yolo)
target_compile_definitions(yolo PRIVATE SIDE_A)
target_link_libraries(yolo kickcat pthread rt)

add_executable(yala vsock.cc)
set_kickcat_properties(yala)
target_compile_definitions(yala PRIVATE SIDE_B)
target_link_libraries(yala kickcat pthread rt)
