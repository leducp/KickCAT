# Copyright (c) 2020 Arduino CMake Toolchain

# No need to include this recursively
if(_BOARDS_INDEX_INCLUDED)
	return()
endif()
set(_BOARDS_INDEX_INCLUDED TRUE)

#******************************************************************************
# Indexing of arduino boards. For indexing arduino boards, arduino platforms
# needs to be indexed using 'IndexArduinoPlatforms' (See PlatformIndex.cmake).

include(CMakeParseArguments)
include(Arduino/Utilities/CommonUtils)
include(Arduino/Utilities/PropertiesReader)
include(Arduino/System/PackagePathIndex)
include(Arduino/System/PlatformIndex)

#==============================================================================
# Index the arduino boards of all the installed arduino platforms from 
# boards.txt file. This function calls the 'IndexArduinoPlatforms' internally
# to index the installed arduino platforms, and hence no need to call it 
# explicitly before calling IndexArduinoBoards. All the indexed arduino 
# boards are stored in the given 'namespace'.
#
# After indexing the boards, a call to 'boards_get_list' returns all the
# installed arduino boards (e.g. uno, nano etc.). To find the property and 
# platform property of a board, the functions 'boards_get_property' and 
# 'boards_get_platform_property' respectively can then be used.
#
# This function also generates a file BoardOptions.cmake, which can be used
# to select an arduino board to setup the toolchain for and set other menu
# options for the board (Use the cache variable ARDUINO_BOARD_OPTIONS_FILE
# to pass the generated/edited or any other pre-drafted board options file).
# If no board is selected (e.g. first time without any board selected), CMake
# will exit with error. If a board is already selected i.e. if the variable
# ARDUINO_BOARD is set to one of the indexed boards, then the variables
# ARDUINO_BOARD_IDENTIFIER and ARDUINO_BOARD_NAME is set appropriately, which
# can further be used in setting up the toolchain (See BoardToolchain.cmake).
#
function(IndexArduinoBoards namespace)

	# Index all the installed arduino platforms first
	IndexArduinoPlatforms("ard_plat")

	# platforms_print_properties("ard_plat")
	platforms_set_parent_scope("ard_plat")

	# For the first time, if not explicitly specified, use the generated
	# BoardOptions.cmake
	if (NOT ARDUINO_BOARD_OPTIONS_FILE AND
		NOT _LAST_USED_ARDUINO_BOARD_OPTIONS_FILE AND
		EXISTS "${CMAKE_BINARY_DIR}/BoardOptions.cmake")

		set(ARDUINO_BOARD_OPTIONS_FILE "${CMAKE_BINARY_DIR}/BoardOptions.cmake")

	endif()

	# Use last used board options file only if there is any change. This is 
	# to allow changing menu options either through BoardOptions.cmake or through
	# CMake GUI. Otherwise, one will be made override of the other which is not
	# user friendly.
	if (NOT ARDUINO_BOARD_OPTIONS_FILE)
		check_board_options_changed(_b_changed)
		if (_b_changed)
			set(ARDUINO_BOARD_OPTIONS_FILE "${_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE}")
		endif()
	endif()

	# Include the board options file.
	if (ARDUINO_BOARD_OPTIONS_FILE)
		include("${ARDUINO_BOARD_OPTIONS_FILE}")
	endif()

	# Generate the board options template. If this is the same file as the earlier
	# included ARDUINO_BOARD_OPTIONS_FILE, the file can get overwritten later if
	# there is a change.
	_board_configure_file(WRITE
		"${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/BoardOptions_FileHeader.cmake.in"
		_board_options_part1)
	set(_board_options_part2 "")
	set(_board_options_part3 "")

	# Read all properties from boards.txt on each platform
	platforms_get_list("ard_plat" _pl_list)
	set(boards_idx 0)
	set("${namespace}/list")
	set(_board_names_list)
	foreach (pl_id IN LISTS _pl_list)

		platforms_get_property("ard_plat" "${pl_id}" "/path" pl_path)
		math(EXPR boards_idx "${boards_idx} + 1")
		properties_read("${pl_path}/boards.txt" "ard_boards.${boards_idx}")
		properties_set_parent_scope("ard_boards.${boards_idx}")

		if (EXISTS "${pl_path}/programmers.txt")
			properties_read("${pl_path}/programmers.txt"
				"ard_prog.${boards_idx}")
			properties_set_parent_scope("ard_prog.${boards_idx}")
		endif()

		# boards list in the platform
		properties_get_list("ard_boards.${boards_idx}" "^[^.]+\\.build\\.board$" _build_board_list)

		# Menu items in the platform
		properties_get_list("ard_boards.${boards_idx}" "^menu\\.[^.]+$" _menu_prefix_list)

		platforms_get_property("ard_plat" "${pl_id}" "name" _pl_name)
		list(APPEND _board_names_list "**** ${_pl_name} ****")
		platforms_get_property("ard_plat" "${pl_id}" "architecture" pl_arch)

		_board_configure_file(APPEND
			"${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/BoardOptions_BoardHeader.cmake.in"
			_board_options_part1)

		# For all the boards in this platform
		set(_board_identifier_list)
		set(b_include_programmers FALSE)
		foreach(_build_board IN LISTS _build_board_list)
			string(REGEX MATCH "^([^.]+)\\.build\\.board$" match "${_build_board}")
			if (NOT match)
				continue()
			endif()

			# Find board prefix used in boards.txt and board name
			set(_board_prefix "${CMAKE_MATCH_1}")
			set(_board_identifier "${pl_arch}.${CMAKE_MATCH_1}")
			properties_get_value("ard_boards.${boards_idx}" "${_board_prefix}.hide"
				_board_hide QUIET DEFAULT "FALSE")
			if (_board_hide)
				continue()
			endif()
			list(APPEND _board_identifier_list "${_board_identifier}")
			set("${namespace}.${_board_identifier}/prop_namespace" "ard_boards.${boards_idx}")
			set("${namespace}.${_board_identifier}/prop_namespace" "ard_boards.${boards_idx}" PARENT_SCOPE)
			set("${namespace}.${_board_identifier}/pl_id" "${pl_id}")
			set("${namespace}.${_board_identifier}/pl_id" "${pl_id}" PARENT_SCOPE)
			properties_get_value("ard_boards.${boards_idx}" "${_board_prefix}.name" _board_name)
			set(_board_name_in_menu "${_board_name} [${_board_identifier}]")
			list(APPEND _board_names_list "${_board_name_in_menu}")

			# Is this the selected board for the project build?
			if ("${ARDUINO_BOARD}" STREQUAL "${_board_name_in_menu}" OR
				"${ARDUINO_BOARD}" STREQUAL "${_board_identifier}")
				set(is_selected_board TRUE)
				set(b_include_programmers TRUE)
				set(_sel_board_prefix "${_board_prefix}")
				set(_sel_board_identifier "${_board_identifier}")
				set(_sel_board_name "${_board_name}")
				set(_board_sel_comment "")
			else()
				set(is_selected_board FALSE)
				set(_board_sel_comment "# ")
			endif()

			_board_configure_file(APPEND
				"${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/BoardOptions_BoardSel.cmake.in"
				_board_options_part1)

			# Populate the menu for the options of the board
			properties_get_list("ard_boards.${boards_idx}" "^${_board_prefix}\\.menu\\.[^.]+\\.[^.]+$"
				_menu_opt_prop_list)

			set(_menu_identifier_list)
			foreach(_menu_opt_prop IN LISTS _menu_opt_prop_list)

				# Find the menu where this menu option should go.
				string(REGEX MATCH "^${_board_prefix}\\.menu\\.([^.]+)\\.([^.]+)$" 
					match "${_menu_opt_prop}")
				set(_menu_prefix "${CMAKE_MATCH_1}")
				set(_menu_opt_prefix "${CMAKE_MATCH_2}")
				properties_get_value("ard_boards.${boards_idx}" "menu.${_menu_prefix}"
					_menu_name QUIET)
				if (NOT _menu_name)
					set(_menu_name "${_menu_prefix}")
				endif()
				properties_get_value("ard_boards.${boards_idx}" 
					"${_board_prefix}.menu.${_menu_prefix}.${_menu_opt_prefix}" _menu_opt_name)

				# Check the identifier or the cache used to define the selection of this
				# option and select in menu if defined. Otherwise select the first option
				# as the default option
				string(MAKE_C_IDENTIFIER "${_board_identifier}.menu.${_menu_prefix}.${_menu_opt_prefix}"
					_menu_opt_identifier)
				string(TOUPPER "${_menu_opt_identifier}" _menu_opt_identifier)
				set(ARDUINO.${_menu_opt_identifier}.NAME "${_menu_opt_name}")

				string(MAKE_C_IDENTIFIER "${_board_identifier}.menu.${_menu_prefix}" _menu_identifier)
				string(TOUPPER "${_menu_identifier}" _menu_identifier)
				set(ARDUINO.${_menu_identifier}.NAME "${_menu_name}")

				set(_menu_var_name "Arduino(${_board_identifier})/${_menu_name}")

				if (ARDUINO_${_menu_opt_identifier})
					set("${_menu_var_name}" "${_menu_opt_name}" 
						CACHE STRING "Select Arduino Board option \"${_menu_name}\"" FORCE)
					set(ARDUINO.${_menu_identifier}.SEL_OPT "${_menu_opt_identifier}")
				elseif ("${${_menu_var_name}}" STREQUAL "${_menu_opt_name}")
					set(ARDUINO.${_menu_identifier}.SEL_OPT "${_menu_opt_identifier}")
				else()
					# Select the first option as the default option
					set("${_menu_var_name}" "${_menu_opt_name}"
						CACHE STRING "Select Arduino Board option \"${_menu_name}\"")
					if (NOT DEFINED ARDUINO.${_menu_identifier}.SEL_OPT)
						set(ARDUINO.${_menu_identifier}.SEL_OPT "${_menu_opt_identifier}")
					endif()
				endif()

				# Set the visibility of this menu containing this option based on whether
				# the board is selected
				if ("${is_selected_board}")
					set_property(CACHE "${_menu_var_name}" PROPERTY TYPE "STRING")
				else()
					set_property(CACHE "${_menu_var_name}" PROPERTY TYPE "INTERNAL")
				endif()

				# Add the menu option to the menu list, so as to define the identifier
				# later corresponding to its selected menu option
				list(FIND _menu_identifier_list "${_menu_identifier}" _menu_idx)
				if ("${_menu_idx}" EQUAL -1)
					list(APPEND _menu_identifier_list "${_menu_identifier}")
					set("ARDUINO.${_menu_identifier}.OPTIONS")
					set_property(CACHE "${_menu_var_name}" PROPERTY STRINGS "")
				endif()

				# Append the option as the menu item and the list
				set_property(CACHE "${_menu_var_name}" APPEND PROPERTY STRINGS
					"${_menu_opt_name}")
				list(APPEND "ARDUINO.${_menu_identifier}.OPTIONS" "${_menu_opt_identifier}")

			endforeach()

			_board_configure_file(APPEND
				"${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/BoardOptions_MenuBoardHdr.cmake.in"
				_board_options_part2)

			# Generate content to a template board options file
			foreach(_menu_identifier IN LISTS _menu_identifier_list)
				set(_menu_name "${ARDUINO.${_menu_identifier}.NAME}")
				_board_configure_file(APPEND
					"${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/BoardOptions_MenuHeader.cmake.in"
					_board_options_part2)
				foreach(_menu_opt_identifier IN LISTS ARDUINO.${_menu_identifier}.OPTIONS)
					set(_menu_opt_name "${ARDUINO.${_menu_opt_identifier}.NAME}")
					if ("${ARDUINO.${_menu_identifier}.SEL_OPT}" STREQUAL "${_menu_opt_identifier}")
						set(_menu_opt_sel_comment "")
					else()
						set(_menu_opt_sel_comment "# ")
					endif()
					_board_configure_file(APPEND
						"${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/BoardOptions_Menuoption.cmake.in"
						_board_options_part2)
				endforeach()
			endforeach()

			if (${is_selected_board})
				set(_sel_board_menu_identifier_list "${_menu_identifier_list}")
			endif()

		endforeach()

		# programmers list in the platform
		properties_get_list("ard_prog.${boards_idx}" "^[^.]+\\.name$" _prog_list)

		_board_configure_file(APPEND
			"${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/BoardOptions_ProgHeader.cmake.in"
			_board_options_part3)

		# For all the programmers in this platform
		set(_prog_id_list)
		foreach(_prog IN LISTS _prog_list)

			string(REGEX MATCH "^([^.]+)\\.name$" match "${_prog}")
			if (NOT match)
				continue()
			endif()

			# Find programmer prefix used in programmers.txt and name
			set(_prog_prefix "${CMAKE_MATCH_1}")
			set(_prog_id "${pl_arch}.${CMAKE_MATCH_1}")
			list(APPEND _prog_id_list "${_prog_id}")
			set("${namespace}/prog.${_prog_id}.prop_namespace"
				"ard_prog.${boards_idx}")
			set("${namespace}/prog.${_prog_id}.prop_namespace"
				"ard_prog.${boards_idx}" PARENT_SCOPE)
			properties_get_value("ard_prog.${boards_idx}"
				"${_prog_prefix}.name" _prog_name)
			set(_prog_name_in_menu "${_prog_name} [${_prog_id}]")
			if (b_include_programmers)
				list(APPEND _prog_names_list "${_prog_name_in_menu}")
			endif()

			# Is this the selected programmer for the project build?
			if ("${ARDUINO_PROGRAMMER}" STREQUAL "${_prog_name_in_menu}" OR
				"${ARDUINO_PROGRAMMER}" STREQUAL "${_prog_id}")
				set(_sel_prog_id "${_prog_id}")
				set(_sel_prog_name "${_prog_name}")
				set(_prog_sel_comment "")
			else()
				set(_prog_sel_comment "# ")
			endif()

			_board_configure_file(APPEND
				"${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/BoardOptions_ProgSel.cmake.in"
				_board_options_part3)

		endforeach()

		# list(JOIN _board_identifier_list ", " _print_list)
		# message(STATUS "Found boards: [${_print_list}]")
		list(APPEND "${namespace}/list" ${_board_identifier_list})

	endforeach()

	set("${namespace}/list" "${${namespace}/list}" PARENT_SCOPE)

	# Write to BoardOptions.cmake template
	set(_old_board_options_content)
	if (EXISTS "${CMAKE_BINARY_DIR}/BoardOptions.cmake")
		file (READ "${CMAKE_BINARY_DIR}/BoardOptions.cmake" _old_board_options_content)
	endif()
	set(_new_board_options_content
		"${_board_options_part1}${_board_options_part2}${_board_options_part3}")
	if (NOT _new_board_options_content STREQUAL _old_board_options_content)
		if (EXISTS "${CMAKE_BINARY_DIR}/BoardOptions.cmake")
			file(REMOVE "${CMAKE_BINARY_DIR}/BoardOptions.cmake.bak")
			file(RENAME "${CMAKE_BINARY_DIR}/BoardOptions.cmake"
				"${CMAKE_BINARY_DIR}/BoardOptions.cmake.bak")
		endif()
		file(WRITE "${CMAKE_BINARY_DIR}/BoardOptions.cmake" "${_new_board_options_content}")
	endif()

	# message("_board_list:${${namespace}/list}")
	set(ARDUINO_BOARD "${ARDUINO_BOARD}" CACHE STRING "Arduino board for which the project is build for")
	set_property(CACHE ARDUINO_BOARD PROPERTY STRINGS ${_board_names_list})

	# message("ARDUINO_BOARD:${ARDUINO_BOARD}")
	set(ARDUINO_BOARD_IDENTIFIER "${_sel_board_identifier}")
	set(ARDUINO_BOARD_NAME "${_sel_board_name}")
	Message(STATUS "Selected Arduino Board: ${ARDUINO_BOARD_NAME} [${ARDUINO_BOARD_IDENTIFIER}]")

	# Arduino programmers
	set(ARDUINO_PROGRAMMER "${ARDUINO_PROGRAMMER}" CACHE STRING "Arduino programmer used for uploading using programmer and for burning bootloader")
	set_property(CACHE ARDUINO_PROGRAMMER PROPERTY STRINGS ${_prog_names_list})

	if (NOT ARDUINO_BOARD_IDENTIFIER)
		return()
	endif()

	set(ARDUINO_BOARD_IDENTIFIER "${ARDUINO_BOARD_IDENTIFIER}" PARENT_SCOPE)
	set(ARDUINO_BOARD_NAME "${ARDUINO_BOARD_NAME}" PARENT_SCOPE)

	set(ARDUINO_PROGRAMMER_ID "${_sel_prog_id}" PARENT_SCOPE)
	set(ARDUINO_PROGRAMMER_NAME "${_sel_prog_name}" PARENT_SCOPE)

	# Display the selected options and also set the identifier corresponding to the selected options
	set(ARDUINO_SEL_MENU_OPT_ID_LIST)
	foreach(_menu_identifier IN LISTS _sel_board_menu_identifier_list)
		set(_menu_name "${ARDUINO.${_menu_identifier}.NAME}")
		set(_menu_opt_identifier "${ARDUINO.${_menu_identifier}.SEL_OPT}")
		set(_menu_opt_name "${ARDUINO.${_menu_opt_identifier}.NAME}")
		set(ARDUINO_${_menu_opt_identifier} TRUE PARENT_SCOPE)
		list(APPEND ARDUINO_SEL_MENU_OPT_ID_LIST "ARDUINO_${_menu_opt_identifier}")
		message(STATUS "Selected board option: \"${_menu_name}\" = \"${_menu_opt_name}\"")
	endforeach()
	set(ARDUINO_SEL_MENU_OPT_ID_LIST "${ARDUINO_SEL_MENU_OPT_ID_LIST}" PARENT_SCOPE)

	# Set Last used Board options file
	if (ARDUINO_BOARD_OPTIONS_FILE)
		set(_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE "${ARDUINO_BOARD_OPTIONS_FILE}"
	        CACHE INTERNAL "" FORCE)
	elseif (NOT _LAST_USED_ARDUINO_BOARD_OPTIONS_FILE)
		# Include it here, so that it becomes a dependency of the build system
		include("${CMAKE_BINARY_DIR}/BoardOptions.cmake")
		set(_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE "${CMAKE_BINARY_DIR}/BoardOptions.cmake"
			CACHE INTERNAL "" FORCE)
	endif()
	file(TIMESTAMP "${_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE}" _ts)
	set(_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE_TS "${_ts}" CACHE INTERNAL "" FORCE)

endfunction()

#==============================================================================
# This function returns all the installed arduino boards. Must be called after
# a call to 'IndexArduinoBoards'.
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoBoards'
# <return_list> [OUT]: The list of installed boards
#
function(boards_get_list namespace return_list)
	if (NOT DEFINED ${namespace}/list)
		message(FATAL_ERROR "Boards namespace ${namespace} not found!!!")
	endif()
	set("${return_list}" "${${namespace}/list}" PARENT_SCOPE)
endfunction()

#==============================================================================
# This function returns the property value of the specified board.
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoBoards'
# <board_identifier> [IN]: board identifier (one of the entries in the list
# returned by 'boards_get_list'
# <prop_name> [IN]: A property corresponding to the board in boards.txt file.
# The board prefix is omitted, when passing the property name.
# <return_value> [OUT]: The value of the property is returned in this variable
#
function(boards_get_property namespace board_identifier prop_name return_value)
	if (NOT DEFINED "${namespace}/list")
		message(FATAL_ERROR "Boards namespace ${namespace} not found!!!")
	endif()

	# If the property starts with '/' it implies a platform property and not JSON property
	string(SUBSTRING "${prop_name}" 0 1 first_letter)
	if ("${first_letter}" STREQUAL "/")
		if (NOT DEFINED "${namespace}.${board_identifier}${prop_name}")
			message(FATAL_ERROR "Board '${board_identifier}' property '${prop_name}' not found in ${namespace}!!!")
		endif()
		set("${return_value}" "${${namespace}.${board_identifier}${prop_name}}" PARENT_SCOPE)
	else()
		if (NOT DEFINED "${namespace}.${board_identifier}/prop_namespace")
			message(FATAL_ERROR "Board ${board_identifier} not found in ${namespace}!!!")
		endif()
		set(prop_namespace "${${namespace}.${board_identifier}/prop_namespace}")
		string(REGEX MATCH "^([^.]+)\\.([^.]+)$" match "${board_identifier}")
		set(_board_prefix "${CMAKE_MATCH_2}")
		properties_get_value("${prop_namespace}" "${_board_prefix}.${prop_name}"
			_value ${ARGN})
		set("${return_value}" "${_value}" PARENT_SCOPE)
	endif()
endfunction()

#==============================================================================
# This function returns all the properties of the given arduino board.
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoBoards'
# <board_identifier> [IN]: board identifier (one of the entries in the list
# returned by 'boards_get_list')
# <pattern> [IN]: Regular expression pattern
# <return_list> [OUT]: The list of all board properties
#
function(boards_get_property_list namespace board_identifier pattern return_list)
	if (NOT DEFINED "${namespace}/list")
		message(FATAL_ERROR "Boards namespace ${namespace} not found!!!")
	endif()
	if (NOT DEFINED "${namespace}.${board_identifier}/prop_namespace")
		message(FATAL_ERROR "Board ${board_identifier} not found in ${namespace}!!!")
	endif()
	set(prop_namespace "${${namespace}.${board_identifier}/prop_namespace}")
	string(REGEX MATCH "^([^.]+)\\.([^.]+)$" match "${board_identifier}")
	set(_board_prefix "${CMAKE_MATCH_2}")
	properties_get_list("${prop_namespace}" "^${_board_prefix}\\.${pattern}" _list)
	foreach (_elem IN LISTS _list)
		string(REGEX REPLACE "^${_board_prefix}\\." "" _elem "${_elem}")
		list(APPEND _list2 "${_elem}")
	endforeach()
	set("${return_list}" "${_list2}" PARENT_SCOPE)
endfunction()

#==============================================================================
# This function returns the platform property value of the specified board.
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoBoards'
# <board_identifier> [IN]: board identifier (one of the entries in the list
# returned by 'boards_get_list')
# <prop_name> [IN]: JSON property name (rooted at the specified platform
# entry, corresponding to the board, within the JSON file)
# <return_value> [OUT]: The value of the property is returned in this variable
#
function(boards_get_platform_property namespace board_identifier prop_name return_value)
	if (NOT DEFINED "${namespace}/list")
		message(FATAL_ERROR "Boards namespace ${namespace} not found!!!")
	endif()
	if (NOT DEFINED "${namespace}.${board_identifier}/pl_id")
		message(FATAL_ERROR "Board ${board_identifier} not found in ${namespace}!!!")
	endif()
	set(pl_id "${${namespace}.${board_identifier}/pl_id}")
	platforms_get_property("ard_plat" "${pl_id}" "${prop_name}" _value ${ARGN})
	set("${return_value}" "${_value}" PARENT_SCOPE)
endfunction()

#==============================================================================
# This function returns the referenced platform property value of the specified
# board. Note that, some platforms do not implement the entire toolchain, but
# refers to other platform(s) for the part of the toolchain functionality and
# also for the Arduino code and libraries.
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoBoards'
# <board_identifier> [IN]: board identifier (one of the entries in the list
# returned by 'boards_get_list')
# <ref_pkg_name> [IN]: Referenced packager name
# <prop_name> [IN]: JSON property name (rooted at the specified platform
# entry, corresponding to the referenced platform, within the JSON file)
# <return_value> [OUT]: The value of the property is returned in this variable
#
function(boards_get_ref_platform_property namespace board_identifier
	ref_pkg_name prop_name return_value)

	cmake_parse_arguments(parsed_args "QUIET" "DEFAULT" "" ${ARGN})

	if (NOT DEFINED "${namespace}/list")
		message(FATAL_ERROR "Boards namespace ${namespace} not found!!!")
	endif()
	if (NOT DEFINED "${namespace}.${board_identifier}/pl_id")
		message(FATAL_ERROR "Board ${board_identifier} not found in ${namespace}!!!")
	endif()
	set(pl_id "${${namespace}.${board_identifier}/pl_id}")
	platforms_get_property("ard_plat" "${pl_id}" "architecture" pl_arch)
	platforms_get_id("ard_plat" "${ref_pkg_name}" "${pl_arch}" _ref_pl_id)
	if (NOT _ref_pl_id)
		if (NOT parsed_args_QUIET  AND "${parsed_args_DEFAULT}" STREQUAL "")
			message(FATAL_ERROR
				"Referenced platform ${ref_pkg_name} not found in ${namespace}!!!")
		endif()
		set("${return_value}" "${parsed_args_DEFAULT}" PARENT_SCOPE)
		return()
	endif()
	platforms_get_property("ard_plat" "${_ref_pl_id}" "${prop_name}" _value
		${ARGN})
	set("${return_value}" "${_value}" PARENT_SCOPE)

endfunction()

#==============================================================================
# This function returns the property value of the specified programmer.
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoBoards'
# <prog_id> [IN]: programmer id (${ARDUINO_PROGRAMMER_ID})
# <prop_name> [IN]: A property corresponding to the programmer in
# programmers.txt file. The programmer prefix is omitted, when passing the
# property name.
# <return_value> [OUT]: The value of the property is returned in this variable
#
function(programmer_get_property namespace prog_id prop_name return_value)
	if (NOT DEFINED "${namespace}/list")
		message(FATAL_ERROR "Boards namespace ${namespace} not found!!!")
	endif()
	if (NOT DEFINED "${namespace}/prog.${prog_id}.prop_namespace")
		message(FATAL_ERROR "Programmer ${prog_id} not found in ${namespace}!!!")
	endif()
	set(prop_namespace "${${namespace}/prog.${prog_id}.prop_namespace}")
	string(REGEX MATCH "^([^.]+)\\.([^.]+)$" match "${prog_id}")
	set(_prog_prefix "${CMAKE_MATCH_2}")
	properties_get_value("${prop_namespace}" "${_prog_prefix}.${prop_name}"
		_value ${ARGN})
	set("${return_value}" "${_value}" PARENT_SCOPE)
endfunction()

#==============================================================================
# This function returns all the properties of the given arduino programmer.
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoBoards'
# <prog_id> [IN]: programmer id (${ARDUINO_PROGRAMMER_ID})
# <pattern> [IN]: Regular expression pattern
# <return_list> [OUT]: The list of all board properties
#
function(programmer_get_property_list namespace prog_id pattern return_list)
	if (NOT DEFINED "${namespace}/list")
		message(FATAL_ERROR "Boards namespace ${namespace} not found!!!")
	endif()
	if (NOT DEFINED "${namespace}/prog.${prog_id}.prop_namespace")
		message(FATAL_ERROR "Programmer ${prog_id} not found in ${namespace}!!!")
	endif()
	set(prop_namespace "${${namespace}/prog.${prog_id}.prop_namespace}")
	string(REGEX MATCH "^([^.]+)\\.([^.]+)$" match "${prog_id}")
	set(_prog_prefix "${CMAKE_MATCH_2}")
	properties_get_list("${prop_namespace}" "^${_prog_prefix}\\.${pattern}"
		_list)
	foreach (_elem IN LISTS _list)
		string(REGEX REPLACE "^${_prog_prefix}\\." "" _elem "${_elem}")
		list(APPEND _list2 "${_elem}")
	endforeach()
	set("${return_list}" "${_list2}" PARENT_SCOPE)
endfunction()

#==============================================================================
# Check if the board options have changed since used last
function(check_board_options_changed _ret_var)

	if (NOT _LAST_USED_ARDUINO_BOARD_OPTIONS_FILE)
		set("${_ret_var}" FALSE PARENT_SCOPE)
		return()
	endif()

	if (ARDUINO_BOARD_OPTIONS_FILE)
		check_same_file("${ARDUINO_BOARD_OPTIONS_FILE}"
			"${_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE}" _same_file)
		if (NOT _same_file)
			set("${_ret_var}" TRUE PARENT_SCOPE)
			return()
		endif()
	endif()

	file(TIMESTAMP "${_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE}" _ts)
	if (NOT "${_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE_TS}" STREQUAL "${_ts}")
		set("${_ret_var}" TRUE PARENT_SCOPE)
	else()
		set("${_ret_var}" FALSE PARENT_SCOPE)
	endif()

endfunction()

#==============================================================================
# Implementation functions (Subject to change. DO NOT USE)
#

# Used to configure a string that is part of the generated BoardOptions.txt,
# and append it to the given file
function(_board_configure_file _opt_writer_or_append in_file out_str)
	file(READ "${in_file}" _in_file_content)
	string(CONFIGURE "${_in_file_content}" _out_content @ONLY)
	if ("${_opt_writer_or_append}" STREQUAL WRITE)
		set("${out_str}" "${_out_content}" PARENT_SCOPE)
	else()
		string_append("${out_str}" "${_out_content}")
		set("${out_str}" "${${out_str}}" PARENT_SCOPE)
	endif()
endfunction()

# Add last used board options to configure dependency to ensure that
# cmake is reconfigured on any change to the board options. This is needed
# because we do not include board options file when skipping board indexing
# (in case board options did not change).
if (_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE)
	add_configure_dependency("${_LAST_USED_ARDUINO_BOARD_OPTIONS_FILE}")
endif()

