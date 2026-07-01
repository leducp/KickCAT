cmake_minimum_required(VERSION 3.15)
project(KickCAT VERSION 0.0.0 LANGUAGES C CXX)


# Custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_BINARY_DIR})
include(Config)
include(debug)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(KICKCAT_INSTALL_DEFAULT ON)
else()
  set(KICKCAT_INSTALL_DEFAULT OFF)
endif()

option(BUILD_MASTER_EXAMPLES   "Build master examples" ON)
option(BUILD_SLAVE_EXAMPLES    "Build slave examples" ON)
option(BUILD_UNIT_TESTS        "Build unit tests" OFF)
option(BUILD_SIMULATION        "Build simulation" ON)
option(BUILD_TOOLS             "Build tools" ON)
option(ENABLE_ESI_PARSER       "Enable ESI XML parser" ON)
option(ENABLE_AF_XDP           "Enable AF_XDP socket for improved performance (requires libxdp, libbpf, clang)" OFF)
option(ENABLE_CODE_COVERAGE    "Enable code coverage (requires gcovr)" OFF)
option(ENABLE_ASAN             "Build with AddressSanitizer" OFF)
option(ENABLE_UBSAN            "Build with UndefinedBehaviorSanitizer" OFF)
option(BUILD_EEPROM_EDITOR     "Build EEPROM editor GUI (requires imgui, glfw)" OFF)
option(BUILD_KICKUI            "Build KickUI, the KickCAT GUI (requires imgui, glfw)" OFF)
option(KICKCAT_INSTALL         "Install KickCAT targets, headers, CMake config and pkg-config files" ${KICKCAT_INSTALL_DEFAULT})

include(Install)

if (NUTTX)
  set(BUILD_MASTER_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_UNIT_TESTS      OFF CACHE BOOL "" FORCE)
  set(BUILD_SIMULATION      OFF CACHE BOOL "" FORCE)
  set(BUILD_TOOLS           OFF CACHE BOOL "" FORCE)
  set(ENABLE_ESI_PARSER     OFF CACHE BOOL "" FORCE)
  set(ENABLE_AF_XDP         OFF CACHE BOOL "" FORCE)
  set(ENABLE_CODE_COVERAGE  OFF CACHE BOOL "" FORCE)
  set(ENABLE_ASAN           OFF CACHE BOOL "" FORCE)
  set(ENABLE_UBSAN          OFF CACHE BOOL "" FORCE)
  set(BUILD_EEPROM_EDITOR   OFF CACHE BOOL "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

if (ENABLE_UBSAN)
  # 'alignment' is excluded on purpose: the EtherCAT frame layer reads/writes packed buffers at
  # unaligned offsets (supported on the x86/Cortex-M targets). The other UB checks stay on and abort.
  add_compile_options(-fsanitize=undefined -fno-sanitize=alignment -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=undefined)
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

if (BUILD_EEPROM_EDITOR OR BUILD_KICKUI)
  add_subdirectory(tools/common)
endif()

if (BUILD_EEPROM_EDITOR)
  add_subdirectory(tools/eeprom_editor)
endif()

if (BUILD_KICKUI)
  add_subdirectory(tools/kickui)
endif()

add_subdirectory(py_bindings)

if (BUILD_EEPROM_EDITOR OR BUILD_KICKUI)
add_subdirectory(vendor)
endif()
