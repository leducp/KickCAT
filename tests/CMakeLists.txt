find_package(GTest CONFIG QUIET)

if(GTest_FOUND)
    enable_testing()

    add_executable(kickmsg_unit
        unit/types_test.cc
        unit/region_test.cc
        unit/publisher_test.cc
        unit/subscriber_test.cc
        unit/node_test.cc
    )
    target_link_libraries(kickmsg_unit PRIVATE kickmsg GTest::gtest_main)
    add_test(NAME kickmsg COMMAND kickmsg_unit)
else()
    message(WARNING "GTest not found: unit tests will NOT be built "
                    "(run 'conan install . --output-folder=build --build=missing' in kickmsg/)")
endif()

# Stress test (standalone, no gtest)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/stress_test.cc)
    add_executable(kickmsg_stress_test stress_test.cc)
    target_link_libraries(kickmsg_stress_test PRIVATE kickmsg pthread)

    # TSan variant
    if(KICKMSG_ENABLE_TSAN)
        add_executable(kickmsg_stress_test_tsan stress_test.cc)
        target_link_libraries(kickmsg_stress_test_tsan PRIVATE kickmsg pthread)
        target_compile_options(kickmsg_stress_test_tsan PRIVATE -fsanitize=thread -g -O1)
        target_link_options(kickmsg_stress_test_tsan PRIVATE -fsanitize=thread)
    endif()
endif()
