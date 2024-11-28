
add_subdirectory(state_machine)

if (NUTTX)
    add_subdirectory(nuttx/lan9252)

    if (NUTTX_ARCH_CHIP STREQUAL "xmc4")
        add_subdirectory(nuttx/xmc4800)
    endif()
endif()
