cmake_minimum_required(VERSION 3.16.3)
project(KickCAT)

# Custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
set(WARNINGS_FLAGS "-Wall -Wextra -pedantic -Wcast-qual -Wcast-align -Wduplicated-cond -Wshadow -Wmissing-noreturn")

set(LIB_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AbstractLink.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Bus.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Diagnostics.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Frame.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Link.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/LinkRedundancy.cc
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
  )
elseif(PIKEOS)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/ErrorCategory.cc
  )
endif()

add_library(kickcat ${LIB_SOURCES} ${OS_LIB_SOURCES})
target_include_directories(kickcat PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kickcat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)
set_target_properties(kickcat PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
  COMPILE_FLAGS ${WARNINGS_FLAGS}
)

option(BUILD_UNIT_TESTS "Build unit tests" ON)
if (BUILD_UNIT_TESTS)
  find_package(GTest QUIET)
  if (NOT GTest_FOUND)
    message(STATUS "GTest not found: Unit tests will NOT be built")
  endif()
endif()

if (GTest_FOUND)
  add_executable(kickcat_unit #unit/bus-t.cc
                              unit/diagnostics-t.cc
                              unit/frame-t.cc
                             # unit/link-t.cc
                              unit/mailbox-t.cc
                              unit/prints-t.cc
                              unit/protocol-t.cc
                              unit/slave-t.cc
  )

  target_link_libraries(kickcat_unit kickcat GTest::gmock_main)
  set_target_properties(kickcat_unit PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    COMPILE_FLAGS ${WARNINGS_FLAGS}
  )
  add_test(NAME kickcat COMMAND kickcat_unit WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  option(CODE_COVERAGE "Enable code coverage - gcovr shall be in the PATH" FALSE)
  if (${CODE_COVERAGE})
    include(CodeCoverage)
    append_coverage_compiler_flags()
    set(GCOVR_ADDITIONAL_ARGS "--exclude-unreachable-branches")
    setup_target_for_coverage_gcovr_html(
        NAME coverage
        EXECUTABLE kickcat_unit
        EXCLUDE "unit/*" ".*gtest.*" "example" ".*gmock.*" ".*/OS/.*"
        )
  endif()
endif()

add_subdirectory(examples)
