macro(kickmsg_example target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE kickmsg)
endmacro()

kickmsg_example(hello_pubsub    hello_pubsub.cc)
kickmsg_example(hello_zerocopy  hello_zerocopy.cc)
kickmsg_example(hello_broadcast hello_broadcast.cc)
