# Copyright (c) 2020 Arduino CMake Toolchain

if (_BOARD_BUILD_TARGETS_INCLUDED)
	return()
endif()

set(_BOARD_BUILD_TARGETS_INCLUDED TRUE)

include(CMakeParseArguments)
INCLUDE(Arduino/Utilities/CommonUtils)
INCLUDE(Arduino/Utilities/SourceLocator)
include(Arduino/Utilities/SourceDependency)
include(Arduino/System/BoardToolchain)

# Define ARDUINO_LIB property on the target, to identify Arduino Library
# targets.
define_property(TARGET
                PROPERTY ARDUINO_LIB
                FULL_DOCS "If the target builds an Arduino library, then "
                          "this property holds the name of the library"
                BRIEF_DOCS "Arduino library name")

#******************************************************************************
# Interface functions for building the Arduino platform code and libraries,
# and link it with any target in the cmake project.
#

#==============================================================================
# target_link_arduino_libraries(<target>
#           [<PRIVATE|PUBLIC|INTERFACE> <lib>...] ...
#           [<AUTO_PRIVATE|AUTO_PUBLIC|AUTO_INTERFACE> [<src>...]] ...
#           [IGNORE <lib>...]
#           [OVERRIDE <ard_lib_tgt>...])
#
# Link the given arduino libraries to the given target
#
# Arguments:
#
# <target> [IN]: The target to which the arduino libraries are to be linked
#
# <lib> [IN]: Case-sensitive name of the arduino library (NOT target name).
# This is either native, user-installed or project specific Arduino
# library present in Arduino installation paths and project folders (i.e. 
# within the folders with name libraries or dependencies). A special
# name 'core' implies the Arduino core library (Base platform code).
#
# <src> [IN]: Source files used for the automatic detection of the library
# dependencies. If none provided, the list of sources are taken from the
# target SOURCES property. Typically a single interface header file as
# given in one of the below examples should be sufficient.
#
# <ard_lib_tgt> [IN]: Target added using `add_custom_arduino_library` or
# `add_custom_arduino_core`, used as override for any correspondingly 
# automatically detected Arduino library. See detailed documentation.
#
# The keywords AUTO* enables automatic finding of the dependent Arduino
# libraries included from the source files using regular expressions. 
#
# e.g.
#     # Automatically link with Arduino libraries included in my_lib sources.
#     # All those detected libraries are linked as PUBLIC (AUTO_PUBLIC)
#     target_link_arduino_libraries(my_lib AUTO_PUBLIC)
#
#     # Link explicitly with core, Wire and IRremote libraries
#     target_link_arduino_libraries(my_app PRIVATE core Wire IRremote)
#
#     # Automatically link with Arduino libraries included in my_lib.h as
#     # public, and also libraries detected from SOURCES as private
#     target_link_arduino_libraries(my_lib AUTO_PUBLIC my_lib.h AUTO_PRIVATE)
#
# In the above examples, the <lib> parameters (core, Wire, IRremote etc.)
# are Arduino library names and not CMake targets. An internal CMake target
# may be maintened by the toolchain corresponding to each of these Arduino
# libraries (Such an internal target is not accessible outside the toolchain.
# See add_custom_arduino_core/add_custom_arduino_library for advanced usage
# only).
#
# When using automatic detection, in some unlikely cases there may be 
# false positives (e.g. Arduino libraries under a not-defined preprocessor
# flag), which can be ignored using the IGNORE list. Also if there are any
# false negatives (due to not including the correct library header or due
# to obfuscated code?), those needs to be explicitly linked.
#
# e.g.
#     # Ignore certain automatically detected false positives
#     if (ARDUINO_ARCH_AVR)
#     target_link_arduino_libraries(my_app AUTO_PUBLIC IGNORE Wifi)
#     endif()
#
#     # Explicitly link arduino libraries which was not detected
#     target_link_arduino_libraries(my_app PUBLIC Wifi AUTO_PUBLIC)
#
# When automatic detection of libraries is used, then the target is linked
# with the internally maintained CMake target corresponding to the 
# automatically detected libraries. There can be a potential conflict in
# case if an Arduino library is cutomized and linked with the target (See 
# add_custom_arduino_library/add_custom_arduino_core). To avoid this 
# conflict, automatic linking looks for customized versions of the 
# libraries in INTERACE_LINK_LIBRARIES and LINK_LIBRARIES properties
# of the target (if any), as well as in the OVERRIDE list provided to
# this function and uses them instead.
#
# e.g.
#     # See also add_custom_arduino_library to understand the below example
#     find_arduino_library(Wire lib_path)
#     add_custom_arduino_library(my_arduino_wire Wire "${lib_path}")
#     target_link_libraries(my_app PRIVATE my_arduino_wire)
#     # Assuming that my_app sources use some Arduino library 'X' which
#     # in turn uses 'Wire', then the below OVERRIDE ensures that the 
#     # automatically detected 'X' uses 'my_arduino_wire' instead.
#     target_link_arduino_libraries(my_app AUTO_PRIVATE
#                                   OVERRIDE my_arduino_wire)
#     # The OVERRIDE list above is redundant here because we already
#     # linked with my_arduino_wire and so can be found in LINK_LIBRARIES
#
function (target_link_arduino_libraries target_name)

	# message("target_link_arduino_libraries ${target_name} ${ARGN}")

	cmake_policy(PUSH)

	# Need to link with libraries created in any folder and not just
	# in the current folder
	if (NOT CMAKE_VERSION VERSION_LESS 3.13)
		cmake_policy(SET CMP0079 NEW)
	endif()

	set(keywords
		PRIVATE        # Private-linked Arduino Libraries
		PUBLIC         # Public-linked Arduino Libraries
		INTERFACE      # Interface-linked Arduino Libraries
		AUTO           # Default-linked auto detected Arduino libraries
		AUTO_PRIVATE   # Private-linked auto detected Arduino libraries
		AUTO_PUBLIC    # Public-linked auto detected Arduino libraries
		AUTO_INTERFACE # Interface-linked auto detected Arduino libraries
		IGNORE         # Auto detected library names to be ignored
		OVERRIDE       # Customized targets that overrides auto detection
	)

	# Initialize the lists that collect the arguments
	set(_list_DEFAULT)
	foreach(_keyword IN LISTS keywords)
		set(_list_${_keyword}_present FALSE)
		set(_list_${_keyword})
	endforeach()

	# Parse the arguments into lists 
	set(_list_type "DEFAULT")
	foreach(_arg IN LISTS ARGN)
		list(FIND keywords "${_arg}" _idx)
		if (_idx GREATER_EQUAL 0)
			set(_list_type "${_arg}")
			set(_list_${_list_type}_present TRUE)
		else()
			list(APPEND _list_${_list_type} "${_arg}")
		endif()
	endforeach()

	# If target_name itself is an internally maintained arduino library,
	# use that instead (Undocumented feature, should not be used)
	if (NOT TARGET "${target_name}")
		target_get_arduino_lib("_arduino_lib_${target_name}" _ard_lib_name)
		if (_ard_lib_name)
			set(target_name "_arduino_lib_${target_name}")
		else()
			message(FATAL_ERROR "${target_name} is not a CMake target")
		endif()
	endif()

	# Link with the explicitly provided libraries (without
	# PRIVATE/PUBLIC/INTERFACE keywords
	if (_list_DEFAULT)
		set(empty_list)
		_link_ard_lib_list("${target_name}" _list_DEFAULT ""
			empty_list empty_list)
	endif()

	# Link with the explicitly provided libraries (with
	# PRIVATE/PUBLIC/INTERFACE keywords
	foreach(_link_type IN ITEMS PRIVATE PUBLIC INTERFACE)
		if (_list_${_link_type})
			set(empty_list)
			_link_ard_lib_list("${target_name}" _list_${_link_type}
				"${_link_type}" empty_list empty_list)
		endif()
	endforeach()

	# Include the LINK_LIBS and INTERFACE_LINK_LIBS in override list
	_find_linked_arduino_libs("${target_name}" _override_list)
	list(APPEND _list_OVERRIDE ${_override_list})
	if (_list_OVERRIDE)
		list(REMOVE_DUPLICATES _list_OVERRIDE)
	endif()

	# Link with automatically detectected libraries (with AUTO
	# keyword i.e. default linking without PRIVATE/PUBLIC/INTERFACE)
	if (_list_AUTO_present)
		_get_auto_link_libs("${target_name}" _list_AUTO _list_IGNORE
			 _list_OVERRIDE _link_libs)
		# message("_link_libs:${_link_libs}")
		_link_ard_lib_list("${target_name}" _link_libs ""
			_list_IGNORE _list_OVERRIDE)
	endif()

	# Link with automatically detectected libraries (with AUTO_*
	# keywords i.e. linking with PRIVATE/PUBLIC/INTERFACE)
	foreach(_link_type IN ITEMS PRIVATE PUBLIC INTERFACE)
		if (_list_AUTO_${_link_type}_present)
			_get_auto_link_libs("${target_name}" _list_AUTO_${_link_type}
				 _list_IGNORE _list_OVERRIDE _link_libs)
			# message("_link_libs:${_link_type}:${_link_libs}")
			_link_ard_lib_list("${target_name}" _link_libs "${_link_type}"
				_list_IGNORE _list_OVERRIDE)
		endif()
	endforeach()

	cmake_policy(POP)

endfunction()

#==============================================================================
# This function is to be called for any executable target built in the project
# (Of course, only if the build uses this toolchain i.e. if ARDUINO flag is
# defined).
#
# This function ensures that a binary image is generated that can be readily
# uploaded to the board using the '<make> SERIAL_PORT=<port> upload-<target>'.
# Please note that 'add_executable' typically generates ELF file, which cannot
# be directly uploaded to the board.
#
# This function currently also ensures to setup any other pre/post build 
# actions necessary for building the Arduino image and also printing the
# program/data size of the generated image. 
#
# For example,
#     add_executable(my_app ${MY_APP_SOURCES})
#     if (ARDUINO)
#         target_enable_arduino_upload(my_app)
#     endif()
#
# In the above example, it may also be required to link 'my_app' against the 
# Ardunio 'core' i.e. 'target_link_arduino_libraries(my_app PRIVATE core)'.
# However it is OK if 'core' gets transitively linked indirectly i.e. 
# 'target_link_libraries(my_app PRIVATE my_lib)' where 'my_lib' links with
# 'core' and thus 'my_app' indirectly links with 'core'.
#
function(target_enable_arduino_upload target)

	# TODO check if already set?
	# Also validate if EXE target

	# Add the prebuild, postbuild, prelink and postlink command hooks for the sketch
	_set_arduino_target_hooks("${target}" "sketch.prebuild" PRE_BUILD)
	_set_arduino_target_hooks("${target}" "sketch.postbuild;linking.prelink"
		PRE_LINK) # Yes, as per order
	_set_arduino_target_hooks("${target}" "linking.postlink;objcopy.preobjcopy"
		POST_BUILD) # Yes, as per order

	# Set image generation as a post build event
	arduino_board_get_target_cmd("${target}" "^recipe\\.objcopy\\..*\\.pattern$"
		objcopy_list)
	# message("objcopy_list:${objcopy_list}")
	foreach(objcopy IN LISTS objcopy_list)
		# message("${objcopy}:${${objcopy}}")
		string(REGEX MATCH "^recipe\\.objcopy\\.(.*)\\.pattern" match
			"${objcopy}")
		string(TOUPPER "${CMAKE_MATCH_1}" file_ext)
		separate_arguments(cmd_with_args_list UNIX_COMMAND "${${objcopy}}")
		add_custom_command(TARGET "${target}" POST_BUILD COMMAND
			${cmd_with_args_list}
			COMMENT "Generating ${file_ext} image"
			VERBATIM)
	endforeach()

	_set_arduino_target_hooks("${target}" "objcopy.postobjcopy"
		POST_BUILD) # Yes, as per order

	# Post build event for size calculation
	arduino_board_get_target_cmd("${target}" "^recipe\\.size\\.pattern$"
		size_recipe_list)
	# message("size_recipe_list:${size_recipe_list}")
	foreach(size_recipe IN LISTS size_recipe_list)
		# message("${size_recipe}:${${size_recipe}}")
		separate_arguments(size_recipe_str UNIX_COMMAND "${${size_recipe}}")
		add_custom_command(TARGET "${target}" POST_BUILD COMMAND
			${CMAKE_COMMAND}
			ARGS "-DRECIPE_SIZE_PATTERN=${size_recipe_str}"
			-P "${CMAKE_BINARY_DIR}/FirmwareSizePrint.cmake"
			COMMENT "Calculating '${target}' size"
			VERBATIM)
	endforeach()

	# upload target as a custom target upload-<target>
	set(tool "${ARDUINO_BOARD_UPLOAD_TOOL}")
	if (tool)
		arduino_board_get_target_cmd("${target}"
			"^tools\\.${tool}\\.upload\\.pattern$" serial_upload_pattern)
		arduino_board_get_target_cmd("${target}"
			"^tools\\.${tool}\\.upload\\.network_pattern$" network_upload_pattern)

		# Simplify certain long variables in the pattern
		string(REPLACE "{upload.network." "{network." UPLOAD_NETWORK_PATTERN
			"${UPLOAD_NETWORK_PATTERN}")

		#message("yo: ${serial_upload_pattern}:${${serial_upload_pattern}} \n")
		# message("${network_upload_pattern}:${${network_upload_pattern}}")
		_get_def_env_options("${${serial_upload_pattern}}" _serial_defs)
		_get_def_env_options("${${network_upload_pattern}}" _network_defs)
		separate_arguments(serial_upload_pattern_str UNIX_COMMAND
			"${${serial_upload_pattern}}")
		separate_arguments(network_upload_pattern_str UNIX_COMMAND
			"${${network_upload_pattern}}")
		add_custom_target("upload-${target}" 
			${CMAKE_COMMAND} 
			ARGS "-DTARGET=${target}" "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
				"-DCMAKE_VERBOSE_MAKEFILE=${CMAKE_VERBOSE_MAKEFILE}"
				"-DUPLOAD_SERIAL_PATTERN=${serial_upload_pattern_str}"
				"-DUPLOAD_NETWORK_PATTERN=${network_upload_pattern_str}"
				${_serial_defs}
				${_network_defs}
			-P "${CMAKE_BINARY_DIR}/FirmwareUpload.cmake"
			COMMENT "Uploading '${target}'"
			VERBATIM)
		add_dependencies("upload-${target}" "${target}")
	endif()

	# program target as a custom target program-<target>
	set(tool "${ARDUINO_BOARD_PROGRAM_TOOL}")
	if (ARDUINO_PROGRAMMER_ID AND tool)
		arduino_board_get_target_cmd("${target}"
			"^tools\\.${tool}\\.program\\.pattern$" program_pattern)
		if (program_pattern)
			# message("${program_pattern}:${${program_pattern}}")
			_get_def_env_options("${${program_pattern}}" _program_defs)
			separate_arguments(program_pattern_str UNIX_COMMAND
				"${${program_pattern}}")
			add_custom_target("program-${target}"
				${CMAKE_COMMAND}
				ARGS "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
					"-DCMAKE_VERBOSE_MAKEFILE=${CMAKE_VERBOSE_MAKEFILE}"
					"-DCONFIRM_RECIPE_PATTERN=${program_pattern_str}"
					"-DOPERATION=program-${target}"
					${_program_defs}
				-P "${CMAKE_BINARY_DIR}/ExecuteRecipe.cmake"
				COMMENT "Programming '${target}'"
				VERBATIM)
			add_dependencies("program-${target}" "${target}")
		endif()
	endif()

	# erase target as a custom target erase-<target>
	set(tool "${ARDUINO_BOARD_PROGRAM_TOOL}")
	if (ARDUINO_PROGRAMMER_ID AND tool AND NOT TARGET erase-flash)
		arduino_board_get_target_cmd(""
			"^tools\\.${tool}\\.erase\\.pattern$" erase_pattern)
		if (erase_pattern)
			# message("${erase_pattern}:${${erase_pattern}}")
			_get_def_env_options("${${erase_pattern}}" _erase_defs)
			separate_arguments(erase_pattern_str UNIX_COMMAND
				"${${erase_pattern}}")
			add_custom_target("erase-flash" 
				${CMAKE_COMMAND}
				ARGS "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
					"-DCMAKE_VERBOSE_MAKEFILE=${CMAKE_VERBOSE_MAKEFILE}"
					"-DCONFIRM_RECIPE_PATTERN=${erase_pattern_str}"
					"-DOPERATION=erase-flash"
					${_erase_defs}
				-P "${CMAKE_BINARY_DIR}/ExecuteRecipe.cmake"
				COMMENT "Erasing flash..."
				VERBATIM)
		endif()
	endif()

	# Burn bootloader target as a custom target burn-bootloader
	set(tool "${ARDUINO_BOARD_BOOTLOADER_TOOL}")
	if (ARDUINO_PROGRAMMER_ID AND tool AND NOT TARGET burn-bootloader)
		arduino_board_get_target_cmd("" 
			"^tools\\.${tool}\\.bootloader\\.pattern$" bootloader_pattern)
		if (bootloader_pattern)
			# message("${bootloader_pattern}:${${bootloader_pattern}}")
			_get_def_env_options("${${bootloader_pattern}}" _bootloader_defs)
			separate_arguments(bootloader_pattern_str UNIX_COMMAND
				"${${bootloader_pattern}}")
			add_custom_target("burn-bootloader" 
				${CMAKE_COMMAND}
				ARGS "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
					"-DCMAKE_VERBOSE_MAKEFILE=${CMAKE_VERBOSE_MAKEFILE}"
					"-DCONFIRM_RECIPE_PATTERN=${bootloader_pattern_str}"
					"-DOPERATION=burn-bootloader"
					${_bootloader_defs}
				-P "${CMAKE_BINARY_DIR}/ExecuteRecipe.cmake"
				COMMENT "Burning bootloader..."
				VERBATIM)
		endif()
	endif()

endfunction()

#==============================================================================
# find_arduino_library(<lib> <return_lib_path> 
#                      [HINTS path1 [path2 ...]]
#                      [PATH_SUFFIXES suffix1 [suffix2 ...]]
#                      [NO_DEFAULT_PATH]
#                      [QUIET])
#
# Search for the given arduino library in the standard and/or hinted paths. This
# function is required only if there is better control needed in searching and
# adding an arduino library explicitly (See 'add_custom_arduino_library'). Otherwise
# 'target_link_arduino_libraries'is sufficient for simple usage.
#
# This function returns the path containing the arduino library found and it is
# ensured that the returned library is compatible with the arduino board being 
# built for.
#
# Arguments:
# <lib> [IN]: Name of the arduino library to search
# <return_lib_path> [OUT]: The path containing the library is returned in this 
# variable
#
# Options:
# HINTS: Specify directories to search in addition to the default locations.
# PATH_SUFFIXES: Specify additional subdirectories to check below each directory 
# location otherwise considered.
# NO_DEFAULT_PATH: If specified, no default locations are added to the search
# QUIET: If specified, an error will not be generated if the library is not
# found
#
# e.g. find_arduino_library(Wire lib_path)
#
# In the above example, on return of the function, "${lib_path}" contains the
# path containing the 'Wire' library, which can be used to add this library 
# using the 'add_custom_arduino_library' function.
function(find_arduino_library lib return_lib_path)

	cmake_parse_arguments(parsed_args "NO_DEFAULT_PATH;QUIET" "" "HINTS;PATHS;PATH_SUFFIXES" ${ARGN})

	unset(search_paths)
	if (NOT parsed_args_NO_DEFAULT_PATH)
		list(APPEND search_paths
			"${CMAKE_CURRENT_SOURCE_DIR}"
			"${CMAKE_SOURCE_DIR}"
			${ARDUINO_LIBRARIES_SEARCH_PATHS_EXTRA}
			${ARDUINO_SKETCHBOOK_PATH}
			${ARDUINO_BOARD_RUNTIME_PLATFORM_PATH}
			${ARDUINO_CORE_SPECIFIC_PLATFORM_PATH}
			${ARDUINO_INSTALL_PATH}
		)
	endif()

	list(APPEND search_paths
		${parsed_args_HINTS}
		${parsed_args_PATHS}
	)

	if (NOT ARDUINO_LIB_${lib}_PATH)
		_library_search_process("${lib}" search_paths parsed_args_PATH_SUFFIXES
			"ARDUINO_LIB_${lib}_PATH")
		if (ARDUINO_LIB_${lib}_PATH OR NOT parsed_args_QUIET)
			set(ARDUINO_LIB_${lib}_PATH "${ARDUINO_LIB_${lib}_PATH}"
				CACHE STRING
				"Path found containing the arduino library ${lib}" FORCE)
		endif()
		if (ARDUINO_LIB_${lib}_PATH)
			message(STATUS "Found Arduino Library ${lib}: ${ARDUINO_LIB_${lib}_PATH}")
		endif()
	endif()


	# Error message if not found
	if (NOT ARDUINO_LIB_${lib}_PATH)
		if (NOT parsed_args_QUIET)
			message(SEND_ERROR "Arduino library ${lib} could not be found in "
					"${search_paths}")
		endif()
		set("${return_lib_path}" "${lib}-NOTFOUND" PARENT_SCOPE)
		return()
	endif()

	# message("find_arduino_library(\"${lib}\":${ARDUINO_LIB_${lib}_PATH})")
	set("${return_lib_path}" "${ARDUINO_LIB_${lib}_PATH}" PARENT_SCOPE)

endfunction()

#==============================================================================
# add_custom_arduino_library(<target_name> <lib> [PATH lib_path])
#
# Add a target that builds the arduino library present in the given directory. Adding a
# library using this function is typically not needed. It is sufficient to use 
# target_link_arduino_libraries. However this function may provide better control.
# For example, if it is required to add 2 versions of the same library with different
# compile options, then this function can be handy. A target added using this function
# is linked with other targets using the standard 'target_link_libraries' and not
# target_link_arduino_libraries.
#
# Arguments:
# <target_name> [IN]: Name of the new target to be added
# <lib> [IN]: Name of the arduino library
# [lib_path] [IN]: Optional root directory containing the Arduino library.
# This can be obtained using 'find_arduino_library' or any other means.
#
# e.g. Equivalent of target_link_arduino_libraries(my_app PRIVATE Wire) is as follows:
#     add_custom_arduino_library(my_arduino_wire Wire)
#     target_link_libraries(my_app PRIVATE my_arduino_wire)
#
# In the above example, due to the availability of target name for the added library 
# (my_arduino_wire), user has better control of the library target.
function(add_custom_arduino_library target lib)

	_add_internal_arduino_library("${target}" "${lib}" ${ARGN})

endfunction()

#==============================================================================
# add_custom_arduino_core(<target_name>)
#
# Add a target that builds the arduino core for the arduino board being built for. 
# Adding the core library using this function is typically not needed. It is sufficient
# to use target_link_arduino_libraries to link with 'core' library. However this function
# may provide better control. For example, if it is required to add 2 versions of the 
# core with different compile options, then this function can be handy. A target added
# using this function is linked with other targets using the standard function 
# 'target_link_libraries' which takes target names and not using the function
# 'target_link_arduino_libraries' which takes library names.
#
# Arguments:
# <target_name> [IN]: Name of the new target to be added
#
# e.g. Equivalent of target_link_arduino_libraries(my_app PRIVATE core) is as follows:
#     add_custom_arduino_core(my_arduino_core)
#     target_link_libraries(my_app PRIVATE my_arduino_core)
#
# In the above example, due to the availability of target name for the core library
# (my_arduino_core), user has better control of the target.
function(add_custom_arduino_core target)

	_add_internal_arduino_core("${target}")

endfunction()

#==============================================================================
# Return the Arduino library corresponding to the given target
function(target_get_arduino_lib target ret_var)
	if (NOT TARGET "${target}")
		set("${ret_var}" "" PARENT_SCOPE)
		return()
	endif()
	get_target_property(_lib_type "${target}" TYPE)
	if ("${_lib_type}" STREQUAL "INTERFACE_LIBRARY")
		set("${ret_var}" "" PARENT_SCOPE)
		return()
	endif()
	get_target_property(_ard_lib_name "${target}" ARDUINO_LIB)
	if (NOT _ard_lib_name)
		set("${ret_var}" "" PARENT_SCOPE)
		return()
	endif()
	set("${ret_var}" "${_ard_lib_name}" PARENT_SCOPE)
endfunction()

#==============================================================================
# Implementation functions (Subject to change. DO NOT USE)
#

SET(ARDUINO_LIBRARIES_SEARCH_PATHS_EXTRA "" CACHE PATH
	"Paths to search for Arduino libraries in addition to standard paths"
)

# Set pre/post build command on the target from the given command hooks of the
# board
function(_set_arduino_target_hooks target hook_id_list hook_type)

	# Add the given hooks for the target
	set(_regex_list)
	foreach(_hook_id IN LISTS hook_id_list)
		string(REPLACE "." "\\." _regex "^recipe.hooks.${_hook_id}.[0-9]+.pattern$")
		list(APPEND _regex_list "${_regex}")
	endforeach()

	#message("${_regex_list}")
	arduino_board_get_target_cmd("${target}" "${_regex_list}" hooks_list)
	#message("hooks_list:${hooks_list}")
	foreach(hook IN LISTS hooks_list)
		# message("${hook}:${${hook}}")
		separate_arguments(cmd_with_args_list UNIX_COMMAND "${${hook}}")
		add_custom_command(TARGET "${target}" ${hook_type} COMMAND ${cmd_with_args_list}
			COMMENT "Executing ${hook} hook"
			VERBATIM)
	endforeach()

endfunction()

# Get the arduino libraries that are included by the given target
# or source list
function(_get_auto_link_libs target_name src_list_var ignore_list_var
	override_list_var ret_list_var)

	set(_ret_list)

	target_get_arduino_lib("${target_name}" _target_ard_lib_name)

	set(override_names)
	foreach(override IN LISTS ${override_list_var})
		target_get_arduino_lib("${override}" _ard_lib_name)
		list(APPEND override_names ${_ard_lib_name})
	endforeach()

	if (NOT "${${src_list_var}}" STREQUAL "")
		set(_target_sources "${${src_list_var}}")
	else()
		get_target_property(_lib_type "${target_name}" TYPE)
		if ("${_lib_type}" STREQUAL "INTERFACE_LIBRARY")
			set(_target_sources "")
		else()
			get_target_property(_target_sources "${target_name}" SOURCES)
		endif()
	endif()

	get_target_property(target_source_dir "${target_name}" SOURCE_DIR)

	foreach(file IN LISTS _target_sources)
		get_filename_component(_file_path "${file}" ABSOLUTE
			BASE_DIR "${target_source_dir}")
		get_source_file_included_headers("${_file_path}" _includes)
		add_configure_dependency("${_file_path}")
		# message("get_source_file_included_headers(${_file_path}:${_includes}:)")
		foreach(inc IN LISTS _includes ITEMS "Arduino")
			list(FIND ${ignore_list_var} "${inc}" _idx)
			# Check if to be ignored
			if (_idx LESS 0 AND NOT "${_target_ard_lib_name}" STREQUAL "${inc}")
				if (inc STREQUAL "Arduino")
					set(_lib "core")
				else()
					set(_lib "${inc}")
				endif()
				list(FIND override_names "${_lib}" _idx)
				if (_idx GREATER_EQUAL 0)
					list(GET ${override_list_var} ${_idx} _cust_lib)
					list(APPEND _ret_list "${_cust_lib}")
				elseif(_lib STREQUAL "core")
					list(APPEND _ret_list "core")
				else()
					find_arduino_library("${inc}" _lib_path QUIET)
					if (_lib_path)
						list(APPEND _ret_list "${inc}")
					endif()
				endif()
			endif()
		endforeach()
	endforeach()

	if (_ret_list)
		list(REMOVE_DUPLICATES _ret_list)
	endif()
	set("${ret_list_var}" "${_ret_list}" PARENT_SCOPE)
endfunction()

# Link the given target with the given list of libraries, creating
# internal targets for the linked libraries as necessary.
function(_link_ard_lib_list target_name lib_list_var link_type
	ignore_list_var override_list_var)

	set(_link_targets)
	foreach(_lib IN LISTS ${lib_list_var})

		# If the library name is already a target building an arduino
		# library, use that. Typically used for convenience or for
		# overridden libraries
		target_get_arduino_lib("${_lib}" _ard_lib_name)
		if (_ard_lib_name)
			set(_link_target "${_lib}")
		elseif (TARGET "_arduino_lib_${_lib}")
			# Already having the internal library
			set(_link_target "_arduino_lib_${_lib}")
		elseif ("${_lib}" STREQUAL "core")
			# library is core, add a library with core sources
			_add_internal_arduino_core(_arduino_lib_core)
			set(_link_target "_arduino_lib_core")
		else()
			# add the library with its sources
			_add_internal_arduino_library("_arduino_lib_${_lib}"
				"${_lib}")
			if (NOT TARGET "_arduino_lib_${_lib}")
				return()
			endif()
			target_link_arduino_libraries("_arduino_lib_${_lib}"
				AUTO_PUBLIC
				IGNORE ${${ignore_list_var}}
				OVERRIDE ${${override_list_var}})
			set(_link_target "_arduino_lib_${_lib}")
		endif()

		if (NOT "${_link_target}" STREQUAL "${target_name}")
			list(APPEND _link_targets "${_link_target}")
		endif()
	endforeach()

	# Finally link the target with all the libraries
	# message("target_link_libraries(\"${target_name}\" ${link_type} ${_link_targets})")
	if (_link_targets)
		target_link_libraries("${target_name}" ${link_type}
			${_link_targets})
	endif()

endfunction()

# Used to add internal arduino library
function(_add_internal_arduino_library target lib)

	cmake_parse_arguments(parsed_args "" "PATH" "" ${ARGN})

	set(_lib_path "${parsed_args_PATH}")
	if (NOT _lib_path)
		find_arduino_library("${lib}" _lib_path)
		if (NOT _lib_path)
			return()
		endif()
	endif()

	# Index the source files
	find_library_source_files("${_lib_path}" lib_sources)
	find_library_header_files("${_lib_path}" lib_headers)
	get_headers_parent_directories("${lib_headers}" include_dirs)

	# TODO Exclude files that do not belong to the board architecture

	# Add the library and set the include directories
	# message("${target}:${lib_sources}:\n${lib_headers}")
	if (NOT lib_sources)
		# Could have added INTERFACE library, but due to CMake limitation
		# of not able to set a property to INTERFACE targets, we have the
		# following workaround of adding a dummy source file
		configure_file(
			${ARDUINO_TOOLCHAIN_DIR}/Arduino/Templates/DummySource.cpp.in
			${CMAKE_CURRENT_BINARY_DIR}/${target}_dummy.cpp
		)
		set(lib_sources "${CMAKE_CURRENT_BINARY_DIR}/${target}_dummy.cpp")
	endif()

	add_library("${target}" STATIC ${lib_headers} ${lib_sources})
	# message("\"${include_dirs}\"")
	target_include_directories(${target} PUBLIC ${include_dirs})

	# Add the prebuild and postbuild command hooks for the library
	_set_arduino_target_hooks("${target}" "libraries.prebuild" PRE_BUILD)
	_set_arduino_target_hooks("${target}" "libraries.postbuild" POST_BUILD)

	# Add ARDUINO_LIB property
	set_target_properties("${target}" PROPERTIES ARDUINO_LIB "${lib}")

endfunction()

# Used to add internal arduino core library
function(_add_internal_arduino_core target)

	## Index the source files
	find_source_files("${ARDUINO_BOARD_BUILD_CORE_PATH}" core_sources RECURSE)
	if (ARDUINO_BOARD_BUILD_VARIANT_PATH)
		find_source_files("${ARDUINO_BOARD_BUILD_VARIANT_PATH}" variant_sources RECURSE)
	else()
		set(variant_sources)
	endif()
	# find_header_files("${ARDUINO_BOARD_BUILD_CORE_PATH}" core_headers)
	# find_header_files("${ARDUINO_BOARD_BUILD_VARIANT_PATH}" variant_headers)

	# On some platforms, files ending with small case .s not taken and cause issues
	# filter this out
	list_filter_exclude_regex(core_sources ".s$")

	# get_headers_parent_directories("${core_headers};${variant_headers}" include_dirs)

	# Add the library and set the include directories
	add_library("${target}" STATIC ${core_headers} ${core_sources}
		${variant_headers} ${variant_sources})
	# target_include_directories(${target} PUBLIC ${include_dirs})
	target_include_directories(${target} PUBLIC
		"${ARDUINO_BOARD_BUILD_CORE_PATH}"
		"${ARDUINO_BOARD_BUILD_VARIANT_PATH}")

	# Add the prebuild and postbuild command hooks for the core
	_set_arduino_target_hooks("${target}" "core.prebuild" PRE_BUILD)
	_set_arduino_target_hooks("${target}" "core.postbuild" POST_BUILD)

	# Add ARDUINO_LIB property
	set_target_properties("${target}" PROPERTIES ARDUINO_LIB "core")

endfunction()

# Find the arduino libraries that have been linked with the given
# target
function(_find_linked_arduino_libs target_name ret_list_var)
	get_target_property(_lib_type "${target_name}" TYPE)
	if ("${_lib_type}" STREQUAL "INTERFACE_LIBRARY")
		set(_link_libs)
	else()
		get_target_property(_link_libs "${target_name}" LINK_LIBRARIES)
		if (NOT _link_libs)
			set(_link_libs)
		endif()
	endif()

	get_target_property(_intf_link_libs "${target_name}"
		INTERFACE_LINK_LIBRARIES)
	if (_intf_link_libs)
		list(APPEND _link_libs ${_intf_link_libs})
	endif()

	foreach(_link_lib IN LISTS _link_libs)
		target_get_arduino_lib("${_link_lib}" _ard_lib_name)
		if (_ard_lib_name)
			if (NOT "${_link_lib}" MATCHES "^_arduino_lib_" AND
				NOT "${_link_lib}" STREQUAL "core")
				list(APPEND _ret_list "${_link_lib}")
			endif()
		endif()
	endforeach()

	set("${ret_list_var}" "${_ret_list}" PARENT_SCOPE)
endfunction()

function(_library_search_process lib search_paths_var search_suffixes_var return_var)

	# message("Searching for ${lib}...")

	# convert lib to a string that can be used in regular expression match
	string(REPLACE "\\" "\\\\" lib_regex "${lib}")
	string(REGEX REPLACE "([].$[*+?|()])" "\\1" lib_regex "${lib_regex}")
	# message("lib_regex:${lib_regex}")
	set(matched_folder_priority 6) # Initialize to higher value of all priorities
	set(matched_arch_priority 3) # Initialize to higher value of all priorities
	set(matched_lib_path "") # The matched library path

	foreach(path IN LISTS "${search_paths_var}")

		set(glob_expressions)
		foreach(suffix IN LISTS "${search_suffixes_var}" ITEMS "libraries" "dependencies")
			list(APPEND glob_expressions "${path}/${suffix}/*")
		endforeach()

		file(GLOB dir_list ${glob_expressions})
		foreach(dir IN LISTS dir_list)
			if (NOT IS_DIRECTORY "${dir}" OR NOT EXISTS "${dir}/library.properties")
				continue()
			endif()

			# Check for folder name match
			get_filename_component(folder_name "${dir}" NAME)
			if ("${folder_name}" STREQUAL "${lib}")
				set(folder_name_priority 1)
			elseif ("${folder_name}" STREQUAL "${lib}-master")
				set(folder_name_priority 2)
			elseif("${folder_name}" MATCHES "^${lib_regex}.*")
				set(folder_name_priority 3)
			elseif("${folder_name}" MATCHES ".*${lib_regex}$")
				set(folder_name_priority 4)
			elseif("${folder_name}" MATCHES ".*${lib_regex}.*")
				set(folder_name_priority 5)
			else()
				continue()
			endif()

			# message("Folder match ${lib}:${dir}:${folder_name_priority}")

			# Check for architecture match
			file(STRINGS "${dir}/library.properties" arch_str REGEX "^architectures=.*")
			string(REGEX MATCH "^architectures=(.*)" arch_list "${arch_str}")
			string(REPLACE "," ";" arch_list "${CMAKE_MATCH_1}")
			string(TOUPPER "${ARDUINO_BOARD_BUILD_ARCH}" board_arch)

			if (arch_list)
				set(arch_match_priority 0) # Match should happen inside the below foreach loop
				foreach(arch IN LISTS arch_list)
					string(STRIP "${arch}" arch)
					string(TOUPPER "${arch}" arch)
					if ("${arch}" STREQUAL "${board_arch}")
						set(arch_match_priority 1)
						break()
					elseif("${arch}" STREQUAL "*")
						set(arch_match_priority 2)
					endif()
				endforeach()
				if (arch_match_priority EQUAL 0)
					continue()
				endif()
			else()
				set(arch_match_priority 2) # unspecified arch assumed to match
			endif()

			# message("Folder/Arch match ${lib}:${dir}:"
			#	"${folder_name_priority}/${matched_folder_priority}:"
			#	"${arch_match_priority}/${matched_arch_priority}")

			# Check for better folder name priority
			if (${folder_name_priority} LESS ${matched_folder_priority})
				set(matched_lib_path "${dir}")
				set(matched_folder_priority "${folder_name_priority}")
				set(matched_arch_priority "${arch_match_priority}")
				continue()
			endif()

			# Check for optimized architecture
			if (${arch_match_priority} LESS ${matched_arch_priority})
				set(matched_lib_path "${dir}")
				set(matched_folder_priority "${folder_name_priority}")
				set(matched_arch_priority "${arch_match_priority}")
				continue()
			endif()

		endforeach()

	endforeach()


	if (NOT matched_lib_path)
		set ("${return_var}" "${lib}-NOTFOUND" PARENT_SCOPE)
		return()
	endif()

	# Although we got the match, let us search for the required header within the folder
	file(STRINGS "${matched_lib_path}/library.properties" incl_list REGEX "^includes=.*")
	string(REGEX MATCH "^includes=(.*)" incl_list "${arch_str}")
	string(REPLACE "," ";" incl_list "${CMAKE_MATCH_1}")

	foreach(h ${incl_list})
		file(GLOB_RECURSE lib_header_path "${matched_lib_path}/${h}.h*")
		if (NOT lib_header_path)
			message(STATUS "Header ${h} for ${lib} is not found.")
			set ("${return_var}" "${lib}-NOTFOUND" PARENT_SCOPE)
			return()
		endif()
	endforeach()

	set ("${return_var}" "${matched_lib_path}" PARENT_SCOPE)

endfunction()

function(_get_def_env_options str return_defs)

	properties_resolve_value_env("${str}" _tmp_str _req_var_list
		_opt_var_list _all_resolved)

	set(_defs)
	foreach(var_name IN LISTS _req_var_list _opt_var_list)
		string(MAKE_C_IDENTIFIER "${var_name}" var_id)
		string(TOUPPER "${var_id}" var_id)
		if (DEFINED "${var_id}")
			list(APPEND _defs "-D${var_id}=${${var_id}}")
			set("${var_id}" "${${var_id}}" CACHE STRING
				"Default value for ${var_id} used in upload scripts")
		endif()
	endforeach()

	set("${return_defs}" "${_defs}" PARENT_SCOPE)

endfunction()
