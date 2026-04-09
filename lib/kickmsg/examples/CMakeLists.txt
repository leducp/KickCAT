add_executable(hello_pubsub    hello_pubsub.cc)
add_executable(hello_zerocopy  hello_zerocopy.cc)
add_executable(hello_broadcast hello_broadcast.cc)
add_executable(hello_diagnose  hello_diagnose.cc)

target_link_libraries(hello_pubsub    PRIVATE kickmsg kickcat)
target_link_libraries(hello_zerocopy  PRIVATE kickmsg kickcat)
target_link_libraries(hello_broadcast PRIVATE kickmsg kickcat)
target_link_libraries(hello_diagnose  PRIVATE kickmsg kickcat)

set_kickcat_properties(hello_pubsub)
set_kickcat_properties(hello_zerocopy)
set_kickcat_properties(hello_broadcast)
set_kickcat_properties(hello_diagnose)
