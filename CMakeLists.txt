cmake_minimum_required(VERSION 3.15)
project(KickCAT)


# Custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_BINARY_DIR})
include(Config)
include(debug)

add_subdirectory(lib)

option(BUILD_UNIT_TESTS "Build unit tests" OFF)
if (BUILD_UNIT_TESTS)
  find_package(GTest QUIET CONFIG)
  if (NOT GTest_FOUND)
    message(FATAL_ERROR "GTest not found: Unit tests will can NOT be built")
  endif()
endif()

if (GTest_FOUND)
  add_executable(kickcat_unit unit/adler32_sum-t.cc
                              unit/bus-t.cc
                              unit/debughelpers-t.cc
                              unit/diagnostics-t.cc
                              unit/error-t.cc
                              unit/frame-t.cc
                              unit/gateway-t.cc
                              unit/kickcat-t.cc
                              unit/link-t.cc
                              unit/mailbox-t.cc
                              unit/prints-t.cc
                              unit/protocol-t.cc
                              unit/slave-t.cc
                              unit/socket-t.cc
                              unit/Time.cc
  )

  target_link_libraries(kickcat_unit kickcat GTest::gmock_main)
  set_kickcat_properties(kickcat_unit)
  add_test(NAME kickcat COMMAND kickcat_unit WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  option(CODE_COVERAGE "Enable code coverage - gcovr shall be in the PATH" FALSE)
  if (${CODE_COVERAGE})
    include(CodeCoverage)
    append_coverage_compiler_flags()
    set(GCOVR_ADDITIONAL_ARGS --exclude-unreachable-branches --exclude-throw-branches)

    set(EXCLUDE_FILES "unit/*" ".*gtest.*" "example" ".*gmock.*" ".*/OS/.*" "tools/*" "conan/*")
    setup_target_for_coverage_gcovr_html(
        NAME coverage
        EXECUTABLE kickcat_unit
        EXCLUDE ${EXCLUDE_FILES}
        )

    setup_target_for_coverage_gcovr_xml(
        NAME coverage_xml
        EXECUTABLE kickcat_unit
        EXCLUDE ${EXCLUDE_FILES}
        )
  endif()
endif()

option(BUILD_EXAMPLES "Build examples" ON)
if (BUILD_EXAMPLES)
  if (UNIX)
      add_subdirectory(examples/master)
  endif()
  if (NUTTX)
    add_subdirectory(examples/slave)
  endif()
endif()

option(BUILD_SIMULATION "Build simulation" ON)
if (BUILD_SIMULATION)
  if (UNIX)
      add_subdirectory(simulation)
  endif()
endif()

option(BUILD_TOOLS "Build tools" ON)
if (BUILD_TOOLS)
  if (UNIX)
    add_subdirectory(tools)
  endif()
endif()
