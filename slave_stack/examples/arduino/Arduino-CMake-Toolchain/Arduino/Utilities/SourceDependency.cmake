# Copyright (c) 2020 Arduino CMake Toolchain
# Copyright (c) 2018 Arduino CMake

# Note: Much of this code copied from Arduino-CMake-NG project and modified
# Thanks to the contributers of the project

# No need to include this recursively
if(_SOURCE_DEPENDENCY_INCLUDED)
	return()
endif()
set(_SOURCE_DEPENDENCY_INCLUDED TRUE)

set(ARDUINO_CMAKE_HEADER_FILES_SUFFIX_REGEX "(\\.[hH]|\\.[hH][hH]|\\.[hH][pP][pP]|\\.[hH][xX][xX])" CACHE STRING
		"Header Files suffix used for regular expression")
set(ARDUINO_CMAKE_HEADER_INCLUDE_REGEX_PATTERN "^[ \t]*#[ \t]*include[ \t]*[<\"]" CACHE STRING
		"Regex pattern matching header inclusion in a source file")
set(ARDUINO_CMAKE_HEADER_NAME_REGEX_PATTERN
		"${ARDUINO_CMAKE_HEADER_INCLUDE_REGEX_PATTERN}(.+)${ARDUINO_CMAKE_HEADER_FILES_SUFFIX_REGEX}[>\"]$" CACHE STRING
		"Regex pattern matching a header's name when wrapped in inclusion line")

include(CMakeParseArguments)
include(Arduino/Utilities/CommonUtils)

#=============================================================================#
# Retrieves all headers includedby a source file. 
# Headers are returned by their name, with extension (such as '.h').
#       _source_file - Path to a source file to get its' included headers.
#       _return_var - Name of variable in parent-scope holding the return value.
#       Returns - List of headers names with extension that are included by the given source file.
#=============================================================================#
function(get_source_file_included_headers _source_file _return_var)

    if (NOT EXISTS "${_source_file}")
        message(SEND_ERROR "Can't find '#includes', source file doesn't exist: ${_source_file}")
		return()
    endif ()

    file(STRINGS "${_source_file}" source_lines) # Loc = Lines of code
    
    list_filter_include_regex(source_lines ${ARDUINO_CMAKE_HEADER_INCLUDE_REGEX_PATTERN})

    # Extract header names from inclusion
    foreach (loc ${source_lines})
        
        string(REGEX MATCH ${ARDUINO_CMAKE_HEADER_NAME_REGEX_PATTERN} match ${loc})
        
		#get_filename_component(header_name "${CMAKE_MATCH_1}" NAME_WE)
		set(header_name "${CMAKE_MATCH_1}")
        list(APPEND headers ${header_name})
    
    endforeach ()

	if (headers)
		list(REMOVE_DUPLICATES headers)
	endif()
    set(${_return_var} ${headers} PARENT_SCOPE)

endfunction()

