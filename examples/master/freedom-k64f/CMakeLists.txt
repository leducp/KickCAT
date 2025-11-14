set(EXAMPLE_SRCS freedom_k64f_example.cc)

add_executable(freedom_k64f_example ${EXAMPLE_SRCS})
target_link_libraries(freedom_k64f_example kickcat)
set_kickcat_properties(freedom_k64f_example)
