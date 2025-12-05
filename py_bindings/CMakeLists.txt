if (SKBUILD)
    set(LIB_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/kickcat_py.cc
        ${CMAKE_SOURCE_DIR}/lib/py_bindings/src/lib_py.cc
        ${CMAKE_SOURCE_DIR}/lib/py_bindings/src/bus_py.cc
        ${CMAKE_SOURCE_DIR}/lib/py_bindings/src/slave_py.cc
        ${CMAKE_SOURCE_DIR}/lib/py_bindings/src/mailbox_py.cc
        )

    find_package(Python
        REQUIRED COMPONENTS Interpreter Development.Module
        OPTIONAL_COMPONENTS Development.SABIModule)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
        find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(kickcat_py ${LIB_SRCS} NB_STATIC STABLE_ABI)
    target_link_libraries(kickcat_py PRIVATE kickcat)
    target_include_directories(kickcat_py PRIVATE ${CMAKE_SOURCE_DIR}/lib/py_bindings/include)
    target_compile_options(kickcat_py PRIVATE -Wno-shadow -Wno-cast-qual -Wno-pedantic)

    # Set the output name to 'kickcat' so Python imports it as 'kickcat'
    set_target_properties(kickcat_py PROPERTIES
        OUTPUT_NAME "kickcat"
    )
    install(TARGETS kickcat_py DESTINATION .)
endif()
