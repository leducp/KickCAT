cmake_minimum_required(VERSION 3.16.3)
project(KickCAT)

# custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(WARNINGS_FLAGS "-Wall -Wextra -pedantic -Wcast-qual -Wcast-align -Wduplicated-cond -Wshadow -Wmissing-noreturn")

add_library(kickcat src/Bus.cc
                    src/CoE.cc
                    src/Frame.cc
                    src/Link.cc
                    src/LinuxSocket.cc
                    src/Mailbox.cc
                    src/protocol.cc
                    src/Slave.cc
                    src/Time.cc
                    )
target_include_directories(kickcat PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kickcat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)
set_target_properties(kickcat PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
  COMPILE_FLAGS ${WARNINGS_FLAGS}
)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.10.0
)
FetchContent_MakeAvailable(googletest)

add_executable(kickcat_unit unit/link-t.cc
                            unit/frame-t.cc
                            unit/bus-t.cc
                            unit/mailbox-t.cc
                            unit/protocol-t.cc
              )

target_link_libraries(kickcat_unit kickcat gtest gtest_main gmock)
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
      EXCLUDE "unit/*" ".*gtest.*" "example" ".*gmock.*"
      )
endif()

add_subdirectory(example)
