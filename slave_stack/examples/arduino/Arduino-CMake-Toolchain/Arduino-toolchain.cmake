# Copyright (c) 2020 Arduino CMake Toolchain

#=============================================================================
# A toolchain for the Arduino compatile boards.
# Please refer to README.md for the usage.

# If the version of CMake used is below 3.7.0, exit with error.
#
# Intended to support CMake version 3.0.0, but there are limitations which
# requires a minimum CMake version of 3.7.0. However, wherever possible, the
# toolchain remains compatible with 3.0.0, looking for some workarounds for
# the limitations in the future. The limitations are captured below.
#
# Version below 3.2.0 has no support for continue() command. Can be fixed.
#
# Version below 3.4.0 has no support for target properties BINARY_DIR,
# SOURCE_DIR etc. These are required in target command generator expressions.
#
# Version below 3.6.0 has issues in identifying try_compile output for 
# static library. So there are some errors during the configuration, but
# may still possibly work.
#
# Version below 3.7.0 has no support for CMAKE_SYSTEM_CUSTOM_CODE, which
# is required when there is some dynamic information, like Board options,
# that needs to be included in the toolchain. Here just including the user
# provided path will not work, because the user variables, cache or root
# binary directory path etc. are not passed to try_compile.

if (CMAKE_VERSION VERSION_LESS 3.7.0)
	message(FATAL_ERROR "CMake version below 3.7.0 unsupported!!!")
endif()

# Save the policy state. We will restore it at the end.
cmake_policy(PUSH)

# Set policy to above 3.0.0
cmake_policy(VERSION 3.0.0)

# Interpret if() arguments without quotes as variables/keywords
if (NOT CMAKE_VERSION VERSION_LESS 3.1)
	cmake_policy(SET CMP0054 NEW)
endif()

#*****************************************************************************
# Set system name and basic information
set(CMAKE_SYSTEM_NAME "Arduino")

# Set module path to enable local modules search
set(ARDUINO_TOOLCHAIN_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_ARDUINO_TOOLCHAIN_PARENT "${CMAKE_PARENT_LIST_FILE}")
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_LIST_DIR}")
set (ARDUINO_TOOLCHAIN_VERSION "1.0")

# Include modules
include(Arduino/System/BoardsIndex)
include(Arduino/System/BoardToolchain)
include(Arduino/System/BoardBuildTargets)

#*****************************************************************************
# For improved speed, indexing of boards is done only once during a 
# cmake invocation. However, this toolchain file is included multiple
# times in multiple contexts (system determination context, separate
# context for each try compile etc.). After indexing, the selected
# board's toolchain info is configured to a generated file that gets
# included in every other inclusion of this toolchain.
if (NOT _BOARD_INDEXING_COMPLETED)
	get_property(_in_try_compile GLOBAL PROPERTY IN_TRY_COMPILE)
	# IN_TRY_COMPILE check seems to be not enough. Check for parent
	# script works, but may be undocumented!
	get_filename_component(parent_script "${_ARDUINO_TOOLCHAIN_PARENT}"
		NAME_WE)
	if (parent_script STREQUAL "CMakeSystem")
		check_board_options_changed(_b_changed)
		if (NOT _b_changed)
			set(_BOARD_INDEXING_COMPLETED TRUE)
		endif()
	endif()
endif()

if (NOT _BOARD_INDEXING_COMPLETED)
	SetupBoardToolchain()
	set(CMAKE_SYSTEM_CUSTOM_CODE
		"include(\"${CMAKE_BINARY_DIR}/ArduinoSystem.cmake\")"
	)
	set (_BOARD_INDEXING_COMPLETED TRUE)
endif()

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
# For libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Workaround for CMAKE_TRY_COMPILE_TARGET_TYPE. For later ESP32 cores this file is missing
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/build_opt.h" "")

# Do not try to link during the configure time, due to the dependency on the
# core, which we do not have a target yet.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

cmake_policy(POP)
