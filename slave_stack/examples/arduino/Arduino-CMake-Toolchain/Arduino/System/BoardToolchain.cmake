# Copyright (c) 2020 Arduino CMake Toolchain

#******************************************************************************
# Setup the compiler toolchain for the selected arduino board (This file has
# a dependency of indexing arduino boards and selecting one of them, see
# BoardsIndex.cmake for details).

# Do not include this recursively
if(_BOARD_TOOLCHAIN_INCLUDED)
	return()
endif()
set(_BOARD_TOOLCHAIN_INCLUDED TRUE)

include(Arduino/Utilities/CommonUtils)
include(Arduino/Utilities/PropertiesReader)
include(Arduino/System/PackagePathIndex)
include(Arduino/System/PlatformIndex)
include(Arduino/System/BoardsIndex)

#==============================================================================
# Setup the toolchain for the Arduino board set in the variable ARDUINO_BOARD.
# This function calls IndexArduinoBoards() internally and then sets up the
# toolchain if ARDUINO_BOARD is one among them (Note: CMake command line can be
# used to pass ARDUINO_BOARD option directly or using ARDUINO_BOARD_OPTIONS_FILE
# which is a file containing the variables ARDUINO_BOARD and other menu options.
# See BoardsIndex.cmake for details).
#
# This function transfers the board properties of the selected board to a
# global namespace. The function 'arduino_board_get_property' can be used to
# query the property of the selected board, without taking the board identifier
# as an argument (unlike the functions like boards_get_property).
#
function (SetupBoardToolchain)

	# First index the boards and identify the user selected board
	IndexArduinoBoards(ard_brd)

	# Selected board must be set, otherwise we cannot setup the toolchain for
	# the board. Also all the properties of the board must be available in this
	# scope, that can be queried using boards_get_platform_property or
	# boards_get_property
	# message("ARDUINO_BOARD_IDENTIFIER:${ARDUINO_BOARD_IDENTIFIER}")
	if (NOT ARDUINO_BOARD_IDENTIFIER)
		return()
	endif()
	set(ARDUINO_BOARD_IDENTIFIER ${ARDUINO_BOARD_IDENTIFIER} PARENT_SCOPE)

	# Set some initial find root paths
	if (ARDUINO_SKETCHBOOK_PATH)
		list(APPEND ARDUINO_FIND_ROOT_PATH "${ARDUINO_SKETCHBOOK_PATH}")
	endif()

	# Read some properties from the board
	_board_get_platform_property("architecture" ARDUINO_BOARD_BUILD_ARCH)
	string(TOUPPER "${ARDUINO_BOARD_BUILD_ARCH}" ARDUINO_BOARD_BUILD_ARCH)
	_board_get_platform_property("/path" ARDUINO_BOARD_RUNTIME_PLATFORM_PATH)

	# First inherit the platform.txt from the referenced platform
	_board_get_property("build.core" _prop_value  QUIET)
	_get_ref_platform("${_prop_value}" _core_pkg_name _build_core)
	if (_core_pkg_name)
		# Read platform.txt and platform.local.txt of referenced platform into
		# the ard_global namespace. Anything read here could be overridden
		# later while transfering properties from board.txt
		_load_platform_properties(ard_global "${_core_pkg_name}")
		_board_get_ref_platform_property("${_core_pkg_name}" "/path"
			ARDUINO_CORE_SPECIFIC_PLATFORM_PATH)
		set(_core_pl_path "${ARDUINO_CORE_SPECIFIC_PLATFORM_PATH}")
	else()
		set(ARDUINO_CORE_SPECIFIC_PLATFORM_PATH)
		set(_core_pl_path "${ARDUINO_BOARD_RUNTIME_PLATFORM_PATH}")
	endif()

	# Read platform.txt and platform.local.txt into the ard_global namespace
	# Anything read here could be overridden later while transfering properties
	# from board.txt
	properties_read("${ARDUINO_BOARD_RUNTIME_PLATFORM_PATH}/platform.txt" ard_global)
	_board_get_platform_property("/local_path" _local_path)
	if (EXISTS "${_local_path}/platform.local.txt")
		properties_read("${_local_path}/platform.local.txt" ard_global)
	endif()

	# Set build.arch
	properties_set_value("ard_global" "build.arch" "${ARDUINO_BOARD_BUILD_ARCH}")

	list(APPEND ARDUINO_FIND_ROOT_PATH "${ARDUINO_BOARD_RUNTIME_PLATFORM_PATH}")
	if (ARDUINO_CORE_SPECIFIC_PLATFORM_PATH)
		list(APPEND ARDUINO_FIND_ROOT_PATH "${_core_pl_path}")
	endif()

	# Set runtime.platform.path
	properties_set_value("ard_global" "runtime.platform.path"
		"${ARDUINO_BOARD_RUNTIME_PLATFORM_PATH}")

	# Set runtime.hardware.path
	_board_get_platform_property("/hw_path" _prop_value)
	properties_set_value("ard_global" "runtime.hardware.path" "${_prop_value}")

	# Set runtime.ide.path, runtime.ide.version and ide_version
	set(_version "10000")
	set(_path "${ARDUINO_TOOLCHAIN_DIR}")
	if (EXISTS "${ARDUINO_INSTALL_PATH}/lib/version.txt")
		file(READ "${ARDUINO_INSTALL_PATH}/lib/version.txt" _version)
                string(STRIP "${_version}" _version)
		if(_version)
			set(_path "${ARDUINO_INSTALL_PATH}")
			string(REPLACE "." "0" _version "${_version}")
		endif()
	endif()
	properties_set_value("ard_global" "runtime.ide.path" "${_path}")
	properties_set_value("ard_global" "runtime.ide.version" "${_version}")
	properties_set_value("ard_global" "ide_version" "${_version}")

	# Set build.core.path and ARDUINO_BOARD_BUILD_CORE_PATH
	set(ARDUINO_BOARD_BUILD_CORE_PATH "${_core_pl_path}/cores/${_build_core}")

	properties_set_value("ard_global" "build.core.path"
		"${ARDUINO_BOARD_BUILD_CORE_PATH}")
	string_escape_quoting(ARDUINO_BOARD_BUILD_CORE_PATH)

	# Set build.variant.path and ARDUINO_BOARD_BUILD_VARIANT_PATH
	_board_get_property("build.variant" _prop_value QUIET DEFAULT "")
	if (_prop_value)
		_get_ref_platform("${_prop_value}" _variant_pkg_name _build_variant)
		if (_variant_pkg_name)
			_board_get_ref_platform_property("${_variant_pkg_name}" "/path"
				_variant_pl_path)
		else()
			set(_variant_pl_path "${ARDUINO_BOARD_RUNTIME_PLATFORM_PATH}")
		endif()
		set(ARDUINO_BOARD_BUILD_VARIANT_PATH
			"${_variant_pl_path}/variants/${_build_variant}")
	else()
		set(ARDUINO_BOARD_BUILD_VARIANT_PATH)
	endif()

	properties_set_value("ard_global" "build.variant.path"
		"${ARDUINO_BOARD_BUILD_VARIANT_PATH}")
	string_escape_quoting(ARDUINO_BOARD_BUILD_VARIANT_PATH)

	# Set build.system.path
	properties_set_value("ard_global" "build.system.path"
		"${ARDUINO_BOARD_RUNTIME_PLATFORM_PATH}/system")

	# Set runtime.os and ARDUINO_BOARD_HOST_NAME
	if (${CMAKE_HOST_APPLE})
		set(ARDUINO_BOARD_HOST_NAME "macosx")
	elseif (${CMAKE_HOST_UNIX})
		set(ARDUINO_BOARD_HOST_NAME "linux") # Is this right?
	elseif (${CMAKE_HOST_WIN32})
		set(ARDUINO_BOARD_HOST_NAME "windows")
	endif()
	properties_set_value("ard_global" "runtime.os" "${ARDUINO_BOARD_HOST_NAME}")

	# Find if the tools used are in a referenced platform. If so, load the
	# properties of the referenced platform and copy the tool's properties
	# from there. Also the runtime.tools.<name>.path will be set from the
	# referenced platform subsequently.
	set(_tool_prop_list "upload.tool" "program.tool" "bootloader.tool")
	set(_tool_recipe_regex_list "upload\\.pattern|upload\\.network_pattern"
		"program\\.pattern|erase\\.pattern" "bootloader\\.pattern")
	list(LENGTH _tool_prop_list _tool_prop_len)
	math(EXPR _tool_last_idx "${_tool_prop_len}-1")
	set(last_tool_pkg_name)
	foreach(_tool_prop_idx RANGE 0 ${_tool_last_idx})
		list(GET _tool_prop_list ${_tool_prop_idx} _tool_prop)
		list(GET _tool_recipe_regex_list ${_tool_prop_idx} _tool_recipe_regex)
		_board_get_property("${_tool_prop}" _tool_name QUIET DEFAULT "")
		if (NOT _tool_name)
			continue()
		endif()
		_get_ref_platform("${_tool_name}" _ref_tool_pkg_name _tool_name)
		if (_ref_tool_pkg_name AND
			NOT "${_ref_tool_pkg_name}" STREQUAL "${_core_pkg_name}")
			list(APPEND _ref_tool_pkgs "${_ref_tool_pkg_name}")
			if (NOT _ref_tool_pkg_name STREQUAL last_tool_pkg_name) # Optimization
				_load_platform_properties(ard_tool_local "${_ref_tool_pkg_name}"
					RESET)
				properties_resolve_all_values(ard_tool_local)
			endif()
			set(last_tool_pkg_name "${_ref_tool_pkg_name}")
			properties_get_list(ard_tool_local
				"^tool\\.${_tool_name}\\.(${_tool_recipe_regex})$"
				_pattern_name_list)
			foreach(_pattern_name IN LISTS _pattern_name_list)
				properties_get_value(ard_tool_local "${_pattern_name}" _pattern)
				properties_set_value(ard_global "${_pattern_name}" "${_pattern}")
			endforeach()
		endif()
	endforeach()
	if (_ref_tool_pkgs)
		list(REMOVE_DUPLICATES _ref_tool_pkgs)
	endif()
	if (_core_pkg_name)
		list(APPEND _ref_tool_pkgs "${_core_pkg_name}")
	endif()
	_board_get_platform_property("/json_pkg" pkg_name)
	list(APPEND _ref_tool_pkgs "${pkg_name}")

	# Set runtime.tools.<name>.path and runtime.tools.<name>-<version>.path
	# of tools from the referenced platform. This includes core referred
	# platform for compilation tools and other *.tool referrd platforms
	set(_tool_root_path)
	foreach(_ref_pkg_name IN LISTS _ref_tool_pkgs)
		_board_get_ref_platform_property("${_ref_pkg_name}" "/tool_path"
			_tool_path)
		_board_get_ref_platform_property("${_ref_pkg_name}"
			"toolsDependencies.N" _num_tools)
		if (${_num_tools} GREATER 0)
			foreach (_tool_idx RANGE 1 "${_num_tools}")
				_board_get_ref_platform_property("${_ref_pkg_name}"
					"toolsDependencies.${_tool_idx}.name" _tool_name)
				_board_get_ref_platform_property("${_ref_pkg_name}"
					"toolsDependencies.${_tool_idx}.version" _tool_version)
				_board_get_ref_platform_property("${_ref_pkg_name}"
					"toolsDependencies.${_tool_idx}.packager" _tool_packager)
				string(REPLACE "{tool_name}" "${_tool_name}" _prop_value
					"${_tool_path}")
				string(REPLACE "{tool_version}" "${_tool_version}" _prop_value
					"${_prop_value}")
				string(REPLACE "{tl_packager}" "${_tool_packager}" _prop_value
					"${_prop_value}")
				properties_set_value("ard_global" "runtime.tools.${_tool_name}.path"
					"${_prop_value}")
				properties_set_value("ard_global"
					"runtime.tools.${_tool_name}-${_tool_version}.path"
					"${_prop_value}")
				list(APPEND _tool_root_path "${_prop_value}")
			endforeach()
		endif()
	endforeach()

	list(REMOVE_DUPLICATES _tool_root_path)
	list(APPEND ARDUINO_FIND_ROOT_PATH ${_tool_root_path})

	# Transfer properties of the selected board and properties corresponding to
	# the selected menu options
	_board_get_property_list(".*" _board_options)
	# message("_board_options:${_board_options}")
	foreach(_option IN LISTS _board_options)
		string(REGEX MATCH "menu\\.([^.]+)\\.([^.]+)\\.?(.*)" match "${_option}")
		if (match)
			set(_menu_prefix "${CMAKE_MATCH_1}")
			set(_menu_opt_prefix "${CMAKE_MATCH_2}")
			set(_menu_opt_action "${CMAKE_MATCH_3}")
			string(MAKE_C_IDENTIFIER
				"${ARDUINO_BOARD_IDENTIFIER}.menu.${_menu_prefix}.${_menu_opt_prefix}"
				OPT_VARIABLE)
			string(TOUPPER "${OPT_VARIABLE}" OPT_VARIABLE)
			# message("match: ${_menu_prefix}:${_menu_opt_prefix}
			# :${_menu_opt_action}:${OPT_VARIABLE}:${${OPT_VARIABLE}}")
			if ("${ARDUINO_${OPT_VARIABLE}}" AND NOT "${_menu_opt_action}"
					STREQUAL "")
				# property of selected menu option of the selected board
				_board_get_property("${_option}" _prop_value)
				# message("MENU: ${_menu_opt_action} => ${_prop_value}")
				properties_set_value("ard_global" "${_menu_opt_action}"
					"${_prop_value}")
			endif()
		else()
			# property of the selected board
			_board_get_property("${_option}" _prop_value)
			properties_set_value("ard_global" "${_option}" "${_prop_value}")
		endif()
	endforeach()

	# Transfer properties of the selected programmer
	# message("ARDUINO_PROGRAMMER_ID:${ARDUINO_PROGRAMMER_ID}")
	if (ARDUINO_PROGRAMMER_ID)
		programmer_get_property_list(ard_brd "${ARDUINO_PROGRAMMER_ID}"
			"" _prog_options)
		# message("_prog_options:${_prog_options}")
		foreach(_option IN LISTS _prog_options)
			programmer_get_property(ard_brd "${ARDUINO_PROGRAMMER_ID}"
				"${_option}" _prop_value)
			# message("${_option}:${_prop_value}")
			properties_set_value("ard_global" "${_option}"
				"${_prop_value}")
		endforeach()
	endif()

	# Arduino build has an assumption that there is only a single sketchbook
	# (single executable). Some of the recipe patterns have these assumptions
	# i.e. generated files from build patterns common to all executables (e.g.
	# core prebuild pattern) could be referred by the executable-specific
	# build patterns (e.g. link pattern). And thus the {build.path} in
	# executable-specific build patterns could mean different paths (either
	# common library build path or executable build path). To arbitrate this,
	# we use heuristics to identify the generated files in common build
	# patterns, and use that path in executable build patterns. However this
	# may not be perfect (TODO override option).
	foreach(_pattern_name_regex
			"recipe\\.hooks\\.sketch\\.prebuild\\.[0-9]+\\.pattern"
			"recipe\\.hooks\\.libraries\\.prebuild\\.[0-9]+\\.pattern"
			"recipe\\.hooks\\.core\\.prebuild\\.[0-9]+\\.pattern")

		properties_get_list(ard_global "${_pattern_name_regex}"
			_pattern_name_list)
		foreach(_pattern_name IN LISTS _pattern_name_list)
			properties_get_value(ard_global "${_pattern_name}" _pattern)
			# FIX: Here we assume certain limited characters in generated file
			# name. Otherwise we will end up eating some subsequent parts of
			# command line as file name (e.g. {build.path}/gen_file.txt:w,
			# where :w should not be part of filename).
			string(REGEX MATCHALL "{build\\.path}/[-a-zA-Z0-9._/]+"
				_pattern_gen_file_list "${_pattern}")
			list(APPEND _gen_file_list "${_pattern_gen_file_list}")
		endforeach()

	endforeach()

	# Now replace the files generated in prebuild patterns with common path in
	# all patterns
	# message("_gen_file_list:${_gen_file_list}")
	string(REPLACE "{build.path}" "{cmake_binary_dir}" _gen_replace_list
		"${_gen_file_list}")
	string(REPLACE "{build.project_name}" "{cmake_project_name}" _gen_replace_list
		"${_gen_replace_list}")
	# message("_gen_replace_list:${_gen_replace_list}")

	# Replace the generated files in prebuild patterns as common generated files
	foreach(_pattern_name_regex "recipe\\..*pattern" "tools\\..*pattern")
		properties_get_list(ard_global "${_pattern_name_regex}" _pattern_name_list)
		foreach(_pattern_name IN LISTS _pattern_name_list)
			properties_get_value(ard_global "${_pattern_name}" _pattern)
			set(_replace_pattern "${_pattern}")
			set(idx 0)
			foreach(_gen_file IN LISTS _gen_file_list)
				list(GET _gen_replace_list ${idx} _replace_file)
				math(EXPR idx "${idx} + 1")
				string(REPLACE "${_gen_file}" "${_replace_file}" _replace_pattern
					"${_replace_pattern}")
			endforeach()
			# message("${_pattern_name}:${_pattern}:${_replace_pattern}")
			properties_set_value(ard_global "${_pattern_name}"
				"${_replace_pattern}")
		endforeach()
	endforeach()

	# before resolving the values, overwrite the host specific properties
	properties_get_list(ard_global ".*\\.${ARDUINO_BOARD_HOST_NAME}$"
		_host_prop_list)
	foreach(_host_prop IN LISTS _host_prop_list)
		properties_get_value(ard_global "${_host_prop}" _prop_value)
		string(REGEX REPLACE "\\.${ARDUINO_BOARD_HOST_NAME}$" "" _prop
			"${_host_prop}")
		properties_set_value(ard_global "${_prop}" "${_prop_value}")
	endforeach()

	# properties_print_all(ard_global)
	# Resolve all the known variables so far. Remaining will be resolved later
	properties_resolve_all_values(ard_global)

	# message("\n\nAll properties in global")
	# properties_print_all(ard_global)

	# Resolve command patterns and set it on parent scope
	_resolve_command_patterns()
	set(ARDUINO_RULE_SET_LIST)
	foreach(rule IN LISTS ARDUINO_RULE_NAMES_LIST)
		set(_rule_str "${ARDUINO_RULE_${rule}}")
		string_escape_quoting(_rule_str)
		string_append(ARDUINO_RULE_SET_LIST
			"set(\"ARDUINO_RULE_${rule}\" \"${_rule_str}\")\n")
	endforeach()

	# Add definitions for menu options
	set(ARDUINO_SEL_MENU_SET_LIST)
	foreach(_menu_opt_id IN LISTS ARDUINO_SEL_MENU_OPT_ID_LIST)
		string_append(ARDUINO_SEL_MENU_SET_LIST
			"set(${_menu_opt_id} TRUE)\nadd_definitions(-D${_menu_opt_id})\n"
		)
	endforeach()

	# CMAKE_C_COMPILER
	# message("ARDUINO_RULE_recipe.c.o.pattern:${ARDUINO_RULE_recipe.c.o.pattern}")
	_resolve_build_rule_properties("recipe.c.o.pattern" _build_cmd
		_build_string)
	set(CMAKE_C_COMPILER "${_build_cmd}")
	set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> ${_build_string}")
	string_escape_quoting(CMAKE_C_COMPILER)
	string_escape_quoting(CMAKE_C_COMPILE_OBJECT)

	# CMAKE_CXX_COMPILER
	_resolve_build_rule_properties("recipe.cpp.o.pattern" _build_cmd
		_build_string)
	set(CMAKE_CXX_COMPILER "${_build_cmd}")
	set(CMAKE_CXX_COMPILE_OBJECT "<CMAKE_CXX_COMPILER> ${_build_string}")
	string_escape_quoting(CMAKE_CXX_COMPILER)
	string_escape_quoting(CMAKE_CXX_COMPILE_OBJECT)

	# CMAKE_ASM_COMPILER
	_resolve_build_rule_properties("recipe.S.o.pattern" _build_cmd
		_build_string)
	if (_build_cmd) # ASM pattern may not be there?
		set(CMAKE_ASM_COMPILER "${_build_cmd}")
		set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> ${_build_string}")
		string_escape_quoting(CMAKE_ASM_COMPILER)
		string_escape_quoting(CMAKE_ASM_COMPILE_OBJECT)
	endif()

	# CMAKE_C_LINK_EXECUTABLE
	_resolve_build_rule_properties("recipe.c.combine.pattern" _build_cmd
		_build_string)
	set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> ${_build_string}")
	string_escape_quoting(CMAKE_C_LINK_EXECUTABLE)

	# CMAKE_CXX_LINK_EXECUTABLE
	set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> ${_build_string}")
	string_escape_quoting(CMAKE_CXX_LINK_EXECUTABLE)

	# CMAKE_C_CREATE_STATIC_LIBRARY
	_resolve_build_rule_properties("recipe.ar.pattern" _build_cmd
		_build_string)
	set(CMAKE_AR "${_build_cmd}")
	set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> ${_build_string}")
	string_escape_quoting(CMAKE_AR)
	string_escape_quoting(CMAKE_C_CREATE_STATIC_LIBRARY)

	# CMAKE_CXX_CREATE_STATIC_LIBRARY
	set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> ${_build_string}")
	string_escape_quoting(CMAKE_CXX_CREATE_STATIC_LIBRARY)

	# properties_set_parent_scope(ard_global)

	list(APPEND ARDUINO_FIND_ROOT_PATH "${ARDUINO_INSTALL_PATH}")

	_find_system_program_path()
	list(APPEND ARDUINO_SYSTEM_PROGRAM_PATH "/bin")
	list(REMOVE_DUPLICATES ARDUINO_SYSTEM_PROGRAM_PATH)

	string(TOUPPER "${_version}" CMAKE_SYSTEM_VERSION)
	properties_get_value(ard_global "build.board" ARDUINO_BOARD)

	# Tool names
	properties_get_value(ard_global "upload.tool" ARDUINO_BOARD_UPLOAD_TOOL
		QUIET DEFAULT "")
	properties_get_value(ard_global "program.tool" ARDUINO_BOARD_PROGRAM_TOOL
		QUIET DEFAULT "")
	properties_get_value(ard_global "bootloader.tool" ARDUINO_BOARD_BOOTLOADER_TOOL
		QUIET DEFAULT "")

	# Generate the cmake scripts for the size calculation and upload
	_gen_arduino_size_script()
	_gen_arduino_upload_script()
	_gen_execute_recipe_script()

	# Set some more standard and useful CMake toolchain variables that are not set
	# in SetupBoardToolchain
	set(CMAKE_SYSTEM_PROCESSOR "${ARDUINO_BOARD_BUILD_ARCH}")
	set(ARDUINO "${CMAKE_SYSTEM_VERSION}")
	set("ARDUINO_ARCH_${CMAKE_SYSTEM_PROCESSOR}" TRUE)
	SET("ARDUINO_${ARDUINO_BOARD}" TRUE)
	string_escape_quoting(ARDUINO_BOARD_RUNTIME_PLATFORM_PATH)

	configure_file(
		"${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/ArduinoSystem.cmake.in"
		"${CMAKE_BINARY_DIR}/ArduinoSystem.cmake" @ONLY
	)

endfunction()

#==============================================================================
# This function returns the property value of the arduino board for which the
# toolchain is setup.
#
# Arguments:
# <prop_name> [IN]: A property corresponding to the board in boards.txt file,
# platform.txt file or other peroperties. The board prefix is omitted, when
# passing the property name.
# <return_value> [OUT]: The value of the property is returned in this variable
#

# Will not work further as the property variables scope is now internal. Any
# variable exposed out side is present in the generated ArduinoSystem.cmake.
#function(arduino_board_get_property prop return_value)
#	properties_get_value(ard_global "${prop}" _value)
#	set("${return_value}" ${_value} PARENT_SCOPE)
#endfunction()

#==============================================================================
# This function returns the command line corresponding to the given command
# name. Depending on the target type (library, executable), different command
# line hooks specified in the board properties are used.
#
# Arguments:
# <target> [IN]: The target for which the command line is returned. This name
# is used in CMake generator expressions within the returned command line, so
# that certain values like include directories are expanded later at the
# CMake generate time.
# <cmd_name_regex_list> [IN]: List of regular expression corresponding to the
# command hook names (see platform.txt in Arduino platform documentation).
# <return_cmd_list> [OUT]: List of command line strings, that need to be
# executed in sequence
#
function (arduino_board_get_target_cmd target cmd_name_regex_list return_cmd_list)

	set(_return_list)
	foreach(_cmd_name_regex IN LISTS cmd_name_regex_list)
		set(_cmd_name_list "${ARDUINO_RULE_NAMES_LIST}")
		list_filter_include_regex(_cmd_name_list "${_cmd_name_regex}")
		foreach (_cmd_name IN LISTS _cmd_name_list)
			set(_cmd "${ARDUINO_RULE_${_cmd_name}}")
			if (NOT _cmd)
				continue()
			endif()

			_resolve_target_properties("${target}" "${_cmd}" _cmd)
			# message("Before excaping: ${_cmd}")
			_escape_arduino_quote("${_cmd}" _cmd)
			# message("After excaping: ${_cmd}")
			set("${_cmd_name}" "${_cmd}" PARENT_SCOPE)
			list(APPEND _return_list "${_cmd_name}")
		endforeach()
	endforeach()
	set("${return_cmd_list}" "${_return_list}" PARENT_SCOPE)

endfunction()

#==============================================================================
# Implementation functions (Subject to change. DO NOT USE)
#

# Return property of the currently selected board
function (_board_get_property prop return_value)
	boards_get_property(ard_brd "${ARDUINO_BOARD_IDENTIFIER}"
		${prop} _return_value ${ARGN})
	set("${return_value}" "${_return_value}" PARENT_SCOPE)
endfunction()

# Return property of the platform of the currently selected board
function (_board_get_platform_property prop return_value)
	boards_get_platform_property(ard_brd
		"${ARDUINO_BOARD_IDENTIFIER}" ${prop} _return_value ${ARGN})
	set("${return_value}" "${_return_value}" PARENT_SCOPE)
endfunction()

# Return property list of the currently selected board
function (_board_get_property_list pattern return_list)
	boards_get_property_list(ard_brd
		"${ARDUINO_BOARD_IDENTIFIER}" ${pattern} _return_list)
	set("${return_list}" "${_return_list}" PARENT_SCOPE)
endfunction()

# Return property of the referenced platform of the currently selected board
function (_board_get_ref_platform_property ref_pkg_name prop return_value)
	boards_get_ref_platform_property(ard_brd
		"${ARDUINO_BOARD_IDENTIFIER}" "${ref_pkg_name}" ${prop}
		_return_value ${ARGN})
	set("${return_value}" "${_return_value}" PARENT_SCOPE)
endfunction()

# Used by arduino_board_get_target_cmd to expand some tool related variables
function(_resolve_tool_properties tool tool_string return_string)

	# TODO override OS specific
	properties_get_list(ard_global "^tools\\.${tool}\\." _tool_properties)
	foreach(_prop IN LISTS _tool_properties)
		properties_get_value(ard_global "${_prop}" _value)
		string(REGEX REPLACE "^tools\\.${tool}\\." "" _local_prop "${_prop}")
		properties_set_value(ard_local "${_local_prop}" "${_value}" RESET)
	endforeach()

	# Transfer verbose properties, no quiet mode for now
	#if (CMAKE_VERBOSE_MAKEFILE)
		set(_suffix "verbose")
	#else()
	#	set(_suffix "quiet")
	#endif()

	properties_get_list(ard_local "^[^.]+\\.params\\.${_suffix}$"
		_action_property_list)
	foreach(_action_property IN LISTS _action_property_list)
		string(REGEX MATCH "^([^.]+)\\.params\\.${_suffix}$" match
			"${_action_property}")
		set(_action "${CMAKE_MATCH_1}")
		properties_get_value(ard_local "${_action_property}" _value)
		properties_set_value(ard_local "${_action}.verbose" "${_value}")
	endforeach()

	properties_resolve_value("${tool_string}" _value "ard_local")
	set("${return_string}" "${_value}" PARENT_SCOPE)

endfunction()

# Function to resolve all command patterns
function (_resolve_command_patterns)

	set(_rule_name_regex_list "^recipe\\..*\\.pattern$"
		"^tools\\..*\\.network_pattern$" "^tools\\..*\\.pattern$")
	set(ARDUINO_RULE_NAMES_LIST)
	foreach(_rule_name_regex IN LISTS _rule_name_regex_list)
		properties_get_list(ard_global "${_rule_name_regex}"
			_rule_name_list)
		# message("_rule_name_list:${_rule_name_list}")
		foreach (_rule_name IN LISTS _rule_name_list)
			list(APPEND ARDUINO_RULE_NAMES_LIST "${_rule_name}")
			properties_get_value(ard_global "${_rule_name}"
				_rule_string)
			if (NOT _rule_string)
				continue()
			endif()

			if("${_rule_name}" MATCHES "^tools\\.([^.]+)")
				_resolve_tool_properties("${CMAKE_MATCH_1}"
					"${_rule_string}" _rule_string)
			endif()

			set("ARDUINO_RULE_${_rule_name}" "${_rule_string}"
				PARENT_SCOPE)
		endforeach()
	endforeach()

	set(ARDUINO_RULE_NAMES_LIST "${ARDUINO_RULE_NAMES_LIST}" PARENT_SCOPE)

endfunction()

# Used by SetupBoardToolchain to expand the command line for the compilers
# (build rules)
function(_resolve_build_rule_properties rule return_cmd return_string)

	if (NOT DEFINED "ARDUINO_RULE_${rule}")
		set("${return_cmd}" "" PARENT_SCOPE)
		return()
	endif()
	set(rule_string "${ARDUINO_RULE_${rule}}")
	string(REGEX REPLACE "^(\"[^\"]+\"|[^ ]+)" "" _match "${rule_string}")
	set(_rule_cmd "${CMAKE_MATCH_1}")
	string(REGEX REPLACE "^\"?([^\"]+)\"?" "\\1" _rule_cmd "${_rule_cmd}")
	set(_rule_string "${_match}")
	get_filename_component(_path "${_rule_cmd}" DIRECTORY)
	get_filename_component(_name "${_rule_cmd}" NAME)
	find_program(_resolve_program "${_name}" PATHS "${_path}" NO_DEFAULT_PATH
		NO_CMAKE_FIND_ROOT_PATH)
	if (_resolve_program)
		set(_rule_cmd "${_resolve_program}")
	endif()
	unset(_resolve_program CACHE)

	# message("_rule_cmd:${_rule_cmd}")
	# message("_rule_string:${_rule_string}")

	string(REPLACE "{build.source.path}" "<TODO_SOURCE_DIR>" _rule_string
		"${_rule_string}")
	string(REPLACE "{cmake_binary_dir}" "${CMAKE_BINARY_DIR}" _rule_string
		"${_rule_string}")
	string(REPLACE "{cmake_project_name}" "${CMAKE_PROJECT_NAME}" _rule_string
		"${_rule_string}")
	if (CMAKE_VERSION VERSION_LESS 3.4.0)
		string(REPLACE "{includes}" "<DEFINES> <FLAGS>" _rule_string
			"${_rule_string}")
	else()
		string(REPLACE "{includes}" "<DEFINES> <INCLUDES> <FLAGS>" _rule_string
			"${_rule_string}")
	endif()
	string(REGEX REPLACE "\"?{source_file}\"?" "<SOURCE>" _rule_string
		"${_rule_string}")
	if ("${rule}" STREQUAL "recipe.ar.pattern")
		string(REGEX REPLACE "\"?{object_file}\"?" "<LINK_FLAGS> <OBJECTS>"
			_rule_string "${_rule_string}")
		string(REGEX REPLACE "\"?{archive_file_path}\"?" "<TARGET>" _rule_string
			"${_rule_string}")
		string(REGEX REPLACE "\"?{build.path}/{archive_file}\"?" "<TARGET>"
			_rule_string "${_rule_string}")
	else()
		STRING(REGEX REPLACE "\"?{build.path}/{build.project_name}\\.elf\"?"
			"<TARGET>" _rule_string "${_rule_string}")
		string(REGEX REPLACE "\"?{build.path}/{archive_file}\"" ""
			_rule_string "${_rule_string}")
		string(REGEX REPLACE "(\"?{archive_file_path}\"?|\"?{archive_file}\"?)"
			"" _rule_string "${_rule_string}")
		string(REGEX REPLACE "\"?{object_file}\"?" "<OBJECT>" _rule_string
			"${_rule_string}")
		string(REPLACE "{object_files}" "<OBJECTS> <LINK_LIBRARIES>" _rule_string
			"${_rule_string}")
	endif()

	# message("_rule_cmd:${_rule_cmd}")
	# message("_rule_string:${_rule_string}")

	string(REPLACE "{build.path}" "${CMAKE_BINARY_DIR}" _rule_string
		"${_rule_string}")
	string(REPLACE "{build.project_name}" "<TARGET_BASE>"
		_rule_string "${_rule_string}")
	# message("Before excaping _rule_string: ${_rule_string}")
	_escape_arduino_quote("${_rule_string}" _rule_string)
	# message("After excaping _rule_string: ${_rule_string}")

	# message("_rule_cmd:${_rule_cmd}")
	# message("_rule_string:${_rule_string}")

	set("${return_cmd}" "${_rule_cmd}" PARENT_SCOPE)
	set("${return_string}" "${_rule_string}" PARENT_SCOPE)

endfunction()

# Used by arduino_board_get_target_cmd to expand some target related variables
function(_resolve_target_properties target target_string return_string)

	string(REPLACE ">" "$<ANGLE-R>" _target_string "${target_string}")
	string(REPLACE "," "$<COMMA>" _target_string "${_target_string}")
	string(REPLACE "{build.source.path}" "$<TARGET_PROPERTY:${target},SOURCE_DIR>"
		_target_string "${_target_string}")
	string(REPLACE "{build.path}" "$<TARGET_PROPERTY:${target},BINARY_DIR>"
		_target_string "${_target_string}")
	_gen_non_empty_string("$<TARGET_PROPERTY:${target},OUTPUT_NAME>"
		"$<TARGET_PROPERTY:${target},NAME>" proj_name)
	string(REPLACE "{build.project_name}" "${proj_name}" _target_string
		"${_target_string}")
	string(REPLACE "{cmake_binary_dir}" "${CMAKE_BINARY_DIR}" _target_string
		"${_target_string}")
	string(REPLACE "{cmake_project_name}" "${CMAKE_PROJECT_NAME}" _target_string
		"${_target_string}")
	string(REPLACE ";" "$<SEMICOLON>" _target_string "${_target_string}")
	set("${return_string}" "${_target_string}" PARENT_SCOPE)

endfunction()

# Util function for a complex generator expression
function (_gen_non_empty_string str1 str2 ret)
	if (CMAKE_VERSION VERSION_LESS 3.9.6)
 		set(${ret} "$<$<BOOL:${str1}>:${str1}>$<$<NOT:$<BOOL:${str1}>>:${str2}>"
			PARENT_SCOPE)
	else()
	 	set(${ret} "$<IF:$<BOOL:${str1}>,${str1},${str2}>" PARENT_SCOPE)
	endif()
endfunction()

# Escape certain quotes for the CMake to function correctly in the
# presence of those quote characters
function(_escape_arduino_quote str return_str)

	string(LENGTH "${str}" _str_len)
	if (${_str_len} EQUAL 0)
		set("${return_str}" "" PARENT_SCOPE)
		return()
	endif()

	set(_state " ")
	set(_result_str "")
	set(_curr_arg "")
	set(_quote_needed 0)
	set(_space_chars "[ \t\r\n]")

	set(_idx 0)
	string(SUBSTRING "${str}" ${_idx} 1 _str_chr)

	while(TRUE)
		math(EXPR _idx "${_idx} + 1")
		string(SUBSTRING "${str}" ${_idx} 1 _nxt_chr)

		if ("${_state}" STREQUAL "\"") # Explicit quoted
			if ((_str_chr STREQUAL "\"" OR
				_str_chr STREQUAL "'") AND
				(_nxt_chr MATCHES "${_space_chars}" OR _nxt_chr STREQUAL ""))
				set(_state " ")
				if(_quote_needed)
					string_append(_result_str "\"${_curr_arg}\"")
				else()
					string_append(_result_str "${_curr_arg}")
				endif()
				set(_curr_arg)
				set(_quote_needed 0)
			elseif(_str_chr STREQUAL "\"")
				string_append(_curr_arg "\\\"")
				set(_quote_needed 1)
			elseif(_str_chr STREQUAL "\\")
				string_append(_curr_arg "\\\\")
				set(_quote_needed 1)
			elseif(_str_chr MATCHES "${_space_chars}")
				string_append(_curr_arg "${_str_chr}")
				set(_quote_needed 1)
			elseif(_str_chr STREQUAL "'")
				string_append(_curr_arg "${_str_chr}")
                set(_quote_needed 1)
			else()
				string_append(_curr_arg "${_str_chr}")
			endif()
		elseif("${_state}" STREQUAL " ") # State after space
			if(_str_chr STREQUAL "\"" OR
				_str_chr STREQUAL "'")
				set(_state "\"")
			elseif(_str_chr MATCHES "${_space_chars}")
				string_append(_result_str "${_str_chr}")
			else()
				set(_state "")
				string_append(_curr_arg "${_str_chr}")
			endif()
		else() # Unquoted argument
			if(_str_chr STREQUAL "\"")
				string_append(_curr_arg "\\\"")
				set(_quote_needed 1)
			elseif(_str_chr STREQUAL "\\")
				string_append(_curr_arg "\\\\")
				set(_quote_needed 1)
			elseif(_str_chr STREQUAL "'")
				string_append(_curr_arg "${_str_chr}")
                set(_quote_needed 1)
			elseif(_str_chr MATCHES "${_space_chars}")
				set(_state " ")
				if(_quote_needed)
					string_append(_result_str "\"${_curr_arg}\"")
				else()
					string_append(_result_str "${_curr_arg}")
				endif()
				set(_curr_arg)
				set(_quote_needed 0)
				string_append(_result_str "${_str_chr}")
			else()
				string_append(_curr_arg "${_str_chr}")
			endif()
		endif()

		if (NOT _idx LESS _str_len)
			if (_curr_arg)
				if(_quote_needed)
					string_append(_result_str "\"${_curr_arg}\"")
				else()
					string_append(_result_str "${_curr_arg}")
				endif()
			endif()
			break()
		endif()

		set(_str_chr "${_nxt_chr}")
	endwhile()

	set("${return_str}" "${_result_str}" PARENT_SCOPE)

endfunction()

# Function to find all the system program paths
function (_find_system_program_path)

	set(_rule_name_regex_list "^recipe\\..*\\.pattern$"
		"^tools\\..*\\.network_pattern$" "^tools\\..*\\.pattern$")
	set(ARDUINO_SYSTEM_PROGRAM_PATH)
	foreach(_rule_name_regex IN LISTS _rule_name_regex_list)
		set(_rule_name_list "${ARDUINO_RULE_NAMES_LIST}")
		list_filter_include_regex(_rule_name_list "${_rule_name_regex}")
		foreach (_rule_name IN LISTS _rule_name_list)
			set(_rule_string "${ARDUINO_RULE_${_rule_name}}")
			if (NOT _rule_string)
				continue()
			endif()

			string(REGEX REPLACE "^(\"[^\"]+\"|[^ ]+)" "" _match
				"${_rule_string}")
			set(_rule_cmd "${CMAKE_MATCH_1}")
			string(REGEX REPLACE "^\"?([^\"]+)\"?" "\\1" _rule_cmd
				"${_rule_cmd}")
			get_filename_component(_rule_cmd_path "${_rule_cmd}" DIRECTORY)
			# message("_rule_cmd_path(${_rule_name}):${_rule_cmd_path}")
			if (NOT _rule_cmd_path)
				continue()
			endif()

			# Check if this command is in one of the find root paths, based on
			# which set the system program path (suffixes used by find_program)
			string(SUBSTRING "${_rule_cmd_path}" 0 1 _first_rule_char)
			foreach(_root_path IN LISTS ARDUINO_FIND_ROOT_PATH)
				string(SUBSTRING "${_root_path}" 0 1 _first_rp_char)
				if(NOT _first_rule_char STREQUAL _first_rp_char)
					continue()
				endif()
				file(RELATIVE_PATH _rel_path "${_root_path}" "${_rule_cmd_path}")
				string(REGEX MATCH "^\\.\\.\\/" _match "${_rel_path}")
				if (NOT _match)
					# message("_rel_path:${_rel_path}")
					list(APPEND ARDUINO_SYSTEM_PROGRAM_PATH "/${_rel_path}")
					break()
				endif()
			endforeach()

		endforeach()
	endforeach()

	set(ARDUINO_SYSTEM_PROGRAM_PATH "${ARDUINO_SYSTEM_PROGRAM_PATH}"
		PARENT_SCOPE)

endfunction()

# Function to generate a cmake script that calculates the size of the compiled
# arduino binary
function (_gen_arduino_size_script)

	properties_get_list(ard_global "^recipe\\.size\\.regex" _size_regex_name_list)
	list(LENGTH _size_regex_name_list SIZE_REGEX_COUNT)

	foreach (size_regex_name IN LISTS _size_regex_name_list)

		properties_get_value(ard_global "${size_regex_name}" _size_regex)
		if (NOT _size_regex)
			continue()
		endif()
		string(REGEX MATCH "^recipe\\.size\\.regex\\.?(.*)" _match "${size_regex_name}")
		set(_size_name "${CMAKE_MATCH_1}")

		if(_size_name STREQUAL "")
			set(_size_name "program")
			properties_get_value(ard_global "upload.maximum_size" _maximum_size DEFAULT 0)
		else()
			properties_get_value(ard_global "upload.maximum_${_size_name}_size"
				_maximum_size DEFAULT 0)
		endif()

		# Filter out non-captured expression from regex which is not supported
		# Instead of that calculate the sub expression match index
		string(REGEX MATCHALL "\\\\.|\\(.." _match_list "${_size_regex}")
		set(_size_match_index 1)
		set(_idx 1)
		foreach(_match IN LISTS _match_list)
			if (_match MATCHES "^\\\\.$")
				continue()
			endif()

			if (NOT _match STREQUAL "^(?:$")
				set(_size_match_index ${_idx})
			endif()

			math(EXPR _idx "${_idx} + 1")
		endforeach()
		string(REGEX REPLACE "\\(\\?:" "(" _size_regex "${_size_regex}")
		string(REGEX REPLACE "\\|\\)" ")" _size_regex "${_size_regex}")

		# Excape certain characters suitable for cmake script generation
		# message("_size_regex:${_size_regex}")
		string(REGEX REPLACE "\\\\s" "[ \t]" _size_regex "${_size_regex}")
		# message("_size_regex:${_size_regex}")

		string(REGEX REPLACE "\\\\|\\\$|\"" "\\\\\\0" _size_regex "${_size_regex}")

		list(APPEND SIZE_REGEX_LIST "${_size_regex}")
		list(APPEND SIZE_NAME_LIST "${_size_name}")
		list(APPEND MAXIMUM_SIZE_LIST "${_maximum_size}")
		list(APPEND SIZE_MATCH_IDX_LIST "${_size_match_index}")

	endforeach()

	if (${SIZE_REGEX_COUNT} GREATER 0)
		configure_file("${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/FirmwareSizePrint.cmake.in"
			"${CMAKE_BINARY_DIR}/FirmwareSizePrint.cmake" @ONLY)
	endif()

endfunction()

# Function to generate a cmake script that uploads the compiled arduino binary
function (_gen_arduino_upload_script)
	configure_file("${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/FirmwareUpload.cmake.in"
		"${CMAKE_BINARY_DIR}/FirmwareUpload.cmake" @ONLY)
endfunction()

# Function to generate a cmake script that uploads the compiled arduino binary
function (_gen_execute_recipe_script)
	configure_file("${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/ExecuteRecipe.cmake.in"
		"${CMAKE_BINARY_DIR}/ExecuteRecipe.cmake" @ONLY)
endfunction()

# Get referenced platform from the string
function(_get_ref_platform str return_pkg_name return_str)
	string(REPLACE ":" ";" str_list "${str}")
	list(LENGTH str_list _len)
	if (_len EQUAL 2)
		list(GET str_list 0 _return_pkg_name)
		list(GET str_list 1 _return_str)
		set("${return_pkg_name}" "${_return_pkg_name}" PARENT_SCOPE)
		set("${return_str}" "${_return_str}" PARENT_SCOPE)
	else()
		set("${return_pkg_name}" "" PARENT_SCOPE)
		set("${return_str}" "${str}" PARENT_SCOPE)
	endif()
endfunction()

function(_load_platform_properties namespace pkg_name)

	_board_get_ref_platform_property("${pkg_name}" "/path" _pl_path)

	# Read platform.txt and platform.local.txt of the given platform into
	# the given namespace.
	properties_read("${_pl_path}/platform.txt" "${namespace}")
	_board_get_ref_platform_property("${pkg_name}" "/local_path" _local_path)
	if (EXISTS "${_local_path}/platform.local.txt")
		properties_read("${_local_path}/platform.local.txt" "${namespace}")
	endif()

	properties_set_parent_scope("${namespace}")

endfunction()
