# Copyright (c) 2020 Arduino CMake Toolchain
# Copyright (c) 2018 Arduino CMake

# Note: Much of this code copied from Arduino-CMake-NG project and modified
# Thanks to the contributers of the project

# No need to include this recursively
if(_SOURCE_LOCATOR_INCLUDED)
	return()
endif()
set(_SOURCE_LOCATOR_INCLUDED TRUE)

set(ARDUINO_CMAKE_C_FILES_PATTERN *.c CACHE STRING
		"C Source Files Pattern")
set(ARDUINO_CMAKE_CXX_FILES_PATTERN *.cc *.cpp *.cxx CACHE STRING
		"CXX Source Files Pattern")
set(ARDUINO_CMAKE_ASM_FILES_PATTERN *.[sS] CACHE STRING
		"ASM Source Files Pattern")
set(ARDUINO_CMAKE_HEADER_FILES_PATTERN *.h *.hh *.hpp *.hxx CACHE STRING
		"Header Files Pattern")
set(ARDUINO_CMAKE_SKETCH_FILES_PATTERN *.ino *.pde CACHE STRING
		"Sketch Files Pattern")
set(ARDUINO_CMAKE_HEADER_FILE_EXTENSION_REGEX_PATTERN ".+\\.h.*$" CACHE STRING
		"Regex pattern matching all header file extensions")

include(CMakeParseArguments)
include(Arduino/Utilities/CommonUtils)

#=============================================================================#
# Finds source files matching the given pattern under the given path.
# Search could also be recursive (With sub-directories) if the optional 'RECURSE' option is passed.
#       _base_path - Top-Directory path to search source files in.
#       [RECURSE] - Whether search should be done recursively or not.
#       _return_var - Name of variable in parent-scope holding the return value.
#       Returns - List of sources in the given path
#=============================================================================#
function(_find_sources _base_path _pattern _return_var)

	cmake_parse_arguments(source_file_search "RECURSE" "" "" ${ARGN})

	# message("Find in ${_base_path}:${_pattern}")

	# Adapt the source files pattern to the given base dir
	set(current_pattern "")
	foreach (pattern_part ${_pattern})
		list(APPEND current_pattern "${_base_path}/${pattern_part}")
	endforeach ()

	if (${source_file_search_RECURSE})
		file(GLOB_RECURSE source_files LIST_DIRECTORIES FALSE ${current_pattern})
	else ()
		file(GLOB source_files LIST_DIRECTORIES FALSE ${current_pattern})
	endif ()

	set(${_return_var} "${source_files}" PARENT_SCOPE)
	# message("Found ${source_files}")

endfunction()

#=============================================================================#
# Finds header files matching the pre-defined header-file pattern under the given path.
# This functions searchs explicitly for header-files such as '*.h'.
# Search could also be recursive (With sub-directories) if the optional 'RECURSE' option is passed.
#       _base_path - Top-Directory path to search source files in.
#       [RECURSE] - Whether search should be done recursively or not.
#       _return_var - Name of variable in parent-scope holding the return value.
#       Returns - List of header files in the given path
#=============================================================================#
function(find_header_files _base_path _return_var)

	_find_sources("${_base_path}" "${ARDUINO_CMAKE_HEADER_FILES_PATTERN}" headers ${ARGN})
	set(${_return_var} "${headers}" PARENT_SCOPE)

endfunction()

#=============================================================================#
# Finds source files matching the pre-defined source-file pattern under the given path.
# This functions searchs explicitly for source-files such as '*.c'.
# Search could also be recursive (With sub-directories) if the optional 'RECURSE' option is passed.
#       _base_path - Top-Directory path to search source files in.
#       [RECURSE] - Whether search should be done recursively or not.
#       _return_var - Name of variable in parent-scope holding the return value.
#       Returns - List of source files in the given path
#=============================================================================#
function(find_source_files _base_path _return_var)

	_find_sources("${_base_path}" "${ARDUINO_CMAKE_C_FILES_PATTERN}" sources ${ARGN})
	if (sources)
		list(APPEND all_sources "${sources}")
		enable_language(C)
	endif()
	_find_sources("${_base_path}" "${ARDUINO_CMAKE_CXX_FILES_PATTERN}" sources ${ARGN})
	if (sources)
		list(APPEND all_sources "${sources}")
		enable_language(CXX)
	endif()
	_find_sources("${_base_path}" "${ARDUINO_CMAKE_ASM_FILES_PATTERN}" sources ${ARGN})
	if (sources)
		list(APPEND all_sources "${sources}")
		enable_language(ASM)
	endif()

	set(${_return_var} "${all_sources}" PARENT_SCOPE)

endfunction()

#=============================================================================#
# Finds sketch files matching the pre-defined sketch-file pattern under the given path.
# This functions searchs explicitly for sketch-files such as '*.ino'.
# Search could also be recursive (With sub-directories) if the optional 'RECURSE' option is passed.
#       _base_path - Top-Directory path to search source files in.
#       [RECURSE] - Whether search should be done recursively or not.
#       _return_var - Name of variable in parent-scope holding the return value.
#       Returns - List of header files in the given path
#=============================================================================#
function(find_sketch_files _base_path _return_var)

	_find_sources("${_base_path}" "${ARDUINO_CMAKE_SKETCH_FILES_PATTERN}" sketches ${ARGN})
	set(${_return_var} "${sketches}" PARENT_SCOPE)

endfunction()

#=============================================================================#
# Gets paths of parent directories from all header files amongst the given sources.
# The list of paths is unique (without duplicates).
#        _sources - List of sources to get include directories from.
#        _return_var - Name of variable in parent-scope holding the return value.
#        Returns - List of directories representing the parent directories of all given headers.
#=============================================================================#
function(get_headers_parent_directories _sources _return_var)

	# Extract header files
	list_filter_include_regex(_sources "${ARDUINO_CMAKE_HEADER_FILE_EXTENSION_REGEX_PATTERN}")
	
	foreach (header_source ${_sources})

		get_filename_component(header_parent_dir ${header_source} DIRECTORY)
		
		list(APPEND parent_dirs ${header_parent_dir})

	endforeach ()

	if (parent_dirs) # Check parent dirs, could be none if there aren't any headers amongst sources
		list(REMOVE_DUPLICATES parent_dirs)
	endif ()

	set(${_return_var} ${parent_dirs} PARENT_SCOPE)

endfunction()


#=============================================================================#
# Recursively finds header files under the given path, excluding those that don't belong to a library,
# such as files under the 'exmaples' directory (In case sources reside under lib's root directory).
#        _base_path - Top-Directory path to search source files in.
#        _return_var - Name of variable in parent-scope holding the return value.
#        Returns - List of source files in the given path
#=============================================================================#
function(find_library_header_files _base_path _return_var)

    if (EXISTS "${_base_path}/src") # 'src' sub-dir exists and should contain sources

        # Headers are always searched recursively under the 'src' sub-dir
        find_header_files("${_base_path}/src" headers RECURSE)

    else ()

        # Both root-dir and 'utility' sub-dir are searched when 'src' doesn't exist
        find_header_files("${_base_path}" root_headers)
        find_header_files("${_base_path}/utility" utility_headers)

        set(headers ${root_headers} ${utility_headers})

    endif ()

    set(${_return_var} "${headers}" PARENT_SCOPE)

endfunction()

#=============================================================================#
# Recursively finds source files under the given path, excluding those that don't belong to a library,
# such as files under the 'exmaples' directory (In case sources reside under lib's root directory).
#        _base_path - Top-Directory path to search source files in.
#        _return_var - Name of variable in parent-scope holding the return value.
#        Returns - List of source files in the given path
#=============================================================================#
function(find_library_source_files _base_path _return_var)

    if (EXISTS "${_base_path}/src")

        # Sources are always searched recursively under the 'src' sub-dir
        find_source_files("${_base_path}/src" sources RECURSE)

    else ()

        # Both root-dir and 'utility' sub-dir are searched when 'src' doesn't exist
        find_source_files("${_base_path}" root_sources)
        find_source_files("${_base_path}/utility" utility_sources)

        set(sources ${root_sources} ${utility_sources})

    endif ()

    set(${_return_var} "${sources}" PARENT_SCOPE)

endfunction()
