cmake_minimum_required(VERSION 3.15)
project(KickCAT)


# Custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_BINARY_DIR})
include(Config)
include(debug)

option(BUILD_MASTER_EXAMPLES   "Build master examples" ON)
option(BUILD_SLAVE_EXAMPLES    "Build slave examples" ON)
option(BUILD_UNIT_TESTS        "Build unit tests" OFF)
option(BUILD_SIMULATION        "Build simulation" ON)
option(BUILD_TOOLS             "Build tools" ON)
option(ENABLE_ESI_PARSER       "Enable ESI XML parser" ON)
option(ENABLE_AF_XDP           "Enable AF_XDP socket for improved performance (requires libxdp, libbpf, clang)" OFF)
option(ENABLE_CODE_COVERAGE    "Enable code coverage (requires gcovr)" OFF)
option(ENABLE_TSAN             "Enable ThreadSanitizer" OFF)
option(BUILD_BENCHMARKS        "Build benchmarks (requires Google Benchmark)" OFF)

if (ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -g)
  add_link_options(-fsanitize=thread)
endif()

if (NUTTX)
  set(BUILD_MASTER_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_UNIT_TESTS      OFF CACHE BOOL "" FORCE)
  set(BUILD_SIMULATION      OFF CACHE BOOL "" FORCE)
  set(BUILD_TOOLS           OFF CACHE BOOL "" FORCE)
  set(ENABLE_ESI_PARSER     OFF CACHE BOOL "" FORCE)
  set(ENABLE_AF_XDP         OFF CACHE BOOL "" FORCE)
  set(ENABLE_CODE_COVERAGE  OFF CACHE BOOL "" FORCE)
endif()


if (BUILD_UNIT_TESTS)
  find_package(GTest QUIET CONFIG)
  if (NOT GTest_FOUND)
    message(FATAL_ERROR "GTest not found: Unit tests will can NOT be built")
  else()
    if (ENABLE_CODE_COVERAGE)
      include(CodeCoverage)
      append_coverage_compiler_flags()

      # Derive versioned gcov (e.g. gcov-15) from the compiler so that gcovr
      # uses the same format as the .gcda/.gcno files produced by the build.
      get_filename_component(_cxx_name ${CMAKE_CXX_COMPILER} NAME)
      string(REGEX REPLACE "^g\\+\\+" "gcov" _gcov_name "${_cxx_name}")
      find_program(_gcov_exe NAMES ${_gcov_name} gcov)

      set(GCOVR_ADDITIONAL_ARGS
          --exclude-unreachable-branches
          --exclude-throw-branches
          --gcov-executable ${_gcov_exe})
    endif()
    enable_testing()
    add_subdirectory(unit)
  endif()
endif()

add_subdirectory(lib)

if (BUILD_MASTER_EXAMPLES OR BUILD_SIMULATION OR BUILD_TOOLS)
  find_package(argparse CONFIG REQUIRED)
endif()

if (BUILD_MASTER_EXAMPLES)
  add_subdirectory(examples/master)
  add_subdirectory(test/integration)
endif()

if (BUILD_SLAVE_EXAMPLES)
  add_subdirectory(examples/slave)
endif()

if (BUILD_SIMULATION)
  find_package(nlohmann_json REQUIRED)
  add_subdirectory(simulation)
endif()

if (BUILD_TOOLS)
  add_subdirectory(tools)
endif()


add_subdirectory(py_bindings)
