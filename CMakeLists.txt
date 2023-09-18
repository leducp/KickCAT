cmake_minimum_required(VERSION 3.15)
project(KickCAT)

# Custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_BINARY_DIR})
include(Config)

set(LIB_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Bus.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Diagnostics.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Frame.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Gateway.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Link.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Mailbox.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Prints.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Slave.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Time.cc
)

if (UNIX)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/UdpDiagSocket.cc
  )
  set(OS_LIBRARIES pthread rt)
elseif(PIKEOS)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/ErrorCategory.cc
  )
  set(OS_LIBRARIES )
endif()

add_library(kickcat ${LIB_SOURCES} ${OS_LIB_SOURCES})
target_include_directories(kickcat PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kickcat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)
target_link_libraries(kickcat pthread rt)
set_kickcat_properties(kickcat)

option(BUILD_UNIT_TESTS "Build unit tests" ON)
if (BUILD_UNIT_TESTS)
  find_package(GTest QUIET CONFIG)
  if (NOT GTest_FOUND)
    message(FATAL_ERROR "GTest not found: Unit tests will can NOT be built")
  endif()
endif()

if (GTest_FOUND)
  add_executable(kickcat_unit unit/bus-t.cc
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
      add_subdirectory(examples)
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
  add_subdirectory(tools)
endif()

option(DEBUG "Debug option" ON)
if (DEBUG)
  add_compile_definitions(DEBUG)
endif()
