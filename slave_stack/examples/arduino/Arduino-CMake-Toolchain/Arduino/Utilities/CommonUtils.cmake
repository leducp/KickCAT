# Copyright (c) 2020 Arduino CMake Toolchain

# No need to include this recursively
if(_TOOLCHAIN_COMMON_UTILS_INCLUDED)
	return()
endif()
set(_TOOLCHAIN_COMMON_UTILS_INCLUDED TRUE)

# List filter is not available in older versions of cmake
# Providing equivalent versions for the same
function(list_filter_include_regex _list_var _regex)

	if (CMAKE_VERSION VERSION_LESS 3.6.3)
		set(_result)
		foreach(_elem IN LISTS "${_list_var}")
			string(REGEX MATCH "${_regex}" _match "${_elem}")
			if (_match)
				list(APPEND _result "${_elem}")
			endif()
		endforeach()
		set("${_list_var}" "${_result}" PARENT_SCOPE)
	else()
		list(FILTER "${_list_var}" INCLUDE REGEX  "${_regex}")
		set("${_list_var}" "${${_list_var}}" PARENT_SCOPE)
	endif()

endfunction()

# List filter is not available in older versions of cmake
# Providing equivalent versions for the same
function(list_filter_exclude_regex _list_var _regex)

	if (CMAKE_VERSION VERSION_LESS 3.6.3)
		set(_result)
		foreach(_elem IN LISTS "${_list_var}")
			string(REGEX MATCH "${_regex}" _match "${_elem}")
			if (NOT _match)
				list(APPEND _result "${_elem}")
			endif()
		endforeach()
		set("${_list_var}" "${_result}" PARENT_SCOPE)
	else()
		list(FILTER "${_list_var}" EXCLUDE REGEX  "${_regex}")
		set("${_list_var}" "${${_list_var}}" PARENT_SCOPE)
	endif()

endfunction()


# List TRANSFORM is not available in older versions of cmake
# Providing equivalent versions for the same
function(list_transform_replace _list_var _regex _replace)
	if (CMAKE_VERSION VERSION_LESS 3.12.4)
		set(_result)
		foreach (_elem IN LISTS "${_list_var}")
			string(REGEX REPLACE "${_regex}" "${_replace}" _new_elem "${_elem}")
			list(APPEND _result "${_new_elem}")
		endforeach()
		set("${_list_var}" "${_result}" PARENT_SCOPE)
	else()
		list(TRANSFORM "${_list_var}" REPLACE "${_regex}" "${_replace}")
		set("${_list_var}" "${${_list_var}}" PARENT_SCOPE)
	endif()
endfunction()

# String APPEND is not available in older versions of cmake
# Providing equivalent versions for the same
function(string_append _string_var _input)
	if (CMAKE_VERSION VERSION_LESS 3.4.3)
		set("${_string_var}" "${${_string_var}}${_input}" PARENT_SCOPE)
	else()
		string(APPEND "${_string_var}" "${_input}")
		set("${_string_var}" "${${_string_var}}" PARENT_SCOPE)
	endif()
endfunction()

# Make a string ready for quoting
function(string_escape_quoting _string_var)
	string(REPLACE "\\" "\\\\" _escaped_str "${${_string_var}}")
	string(REPLACE "\"" "\\\"" _escaped_str "${_escaped_str}")
	set("${_string_var}" "${_escaped_str}" PARENT_SCOPE)
endfunction()

# Check if two files are the same
function(check_same_file file1 file2 _ret_var)
	get_filename_component(x "${file1}" ABSOLUTE)
	get_filename_component(y "${file2}" ABSOLUTE)
	if (x STREQUAL y)
		set("${_ret_var}" TRUE PARENT_SCOPE)
	else()
		set("${_ret_var}" FALSE PARENT_SCOPE)
	endif()
endfunction()

# Add file(s) as configure dependency so that CMake re-runs when
# those files change
function(add_configure_dependency file)
	foreach(item IN LISTS ARGN ITEMS "${file}")
		# message("Configure dependency: ${item}")
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
			"${item}")
	endforeach()
endfunction()

