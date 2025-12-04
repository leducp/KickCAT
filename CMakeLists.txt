cmake_minimum_required(VERSION 3.15)
project(KickCAT)


# Custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_BINARY_DIR})
include(Config)
include(debug)

option(BUILD_EXAMPLES     "Build examples" ON)

if (NOT NUTTX)
  option(ENABLE_ESI_PARSER  "Enable Esi Parser" ON)
  option(BUILD_UNIT_TESTS   "Build unit tests" OFF)
  option(CODE_COVERAGE      "Enable code coverage - gcovr shall be in the PATH" OFF)
  option(BUILD_SIMULATION   "Build simulation" ON)
  option(BUILD_TOOLS        "Build tools" ON)
endif()


if (BUILD_UNIT_TESTS)
  find_package(GTest QUIET CONFIG)
  if (NOT GTest_FOUND)
    message(FATAL_ERROR "GTest not found: Unit tests will can NOT be built")
  else()
    if (CODE_COVERAGE)
      include(CodeCoverage)
      append_coverage_compiler_flags()
      set(GCOVR_ADDITIONAL_ARGS --exclude-unreachable-branches --exclude-throw-branches)
    endif()
    enable_testing()
    add_subdirectory(unit)
  endif()
endif()

add_subdirectory(lib)

if (BUILD_EXAMPLES)
  if (NOT NUTTX)
    add_subdirectory(examples/master)
  endif()
  add_subdirectory(examples/slave)
endif()

if (BUILD_SIMULATION)
  add_subdirectory(simulation)
endif()

if (BUILD_TOOLS)
  add_subdirectory(tools)
endif()

add_subdirectory(kickcat-python)
