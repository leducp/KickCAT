# Copyright (c) 2020 Arduino CMake Toolchain

# No need to include this recursively
if(_JSON_PARSER_INCLUDED)
	return()
endif()
set(_JSON_PARSER_INCLUDED TRUE)

include(CMakeParseArguments)
include(Arduino/Utilities/CommonUtils)

function(json_parse json_content namespace)

	# States: NAME1, NAME_SEP, VALUE, VALUE_0, FIELD_SEP, ARRAY_SEP, NAME2, END
	set(json_parse_state "VALUE")

	string(REGEX MATCHALL "\"(\\\\.|[^\"])*\"|[][{}:,]|true|false|null|-?[0-9]+(\\.[0-9]*)?([eE][+-]?[0-9]+)?" token_list "${json_content}")
	_json_escape_cmake_sqr_brkt(token_list)

	set(curr_field "${namespace}")
	set(open_brace "(")
	set(close_brace ")")

	foreach(var ${token_list})
	
		# message("token:${var}")
		if (${json_parse_state} STREQUAL "VALUE_0")
			if (var STREQUAL close_brace)
				# Array with 0 elements
				_json_leave_context()
				continue()
			else()
				# Start of value for the first element of the array
				# Switch to 0th array element context
				_json_enter_context("1")
				set(json_parse_state "VALUE")
			endif()
		endif()
	
		if (${json_parse_state} STREQUAL "VALUE")
			if (var STREQUAL "{")
				# Start of object value
				_json_set_prop("${curr_field}/type" "object")
				set(json_parse_state "NAME1")
			elseif (var STREQUAL open_brace)
				# Start of array value
				_json_set_prop("${curr_field}/type" "array")
				_json_set_prop("${curr_field}.N" "0")
				set(json_parse_state "VALUE_0")
			elseif(var MATCHES "^(true|false)")
				# Boolean value
				_json_set_prop("${curr_field}" "${var}")
				_json_set_prop("${curr_field}/type" "bool")
				_json_leave_context()
			elseif(var MATCHES "null")
				# NULL value
				_json_set_prop("${curr_field}" "${var}")
				_json_set_prop("${curr_field}/type" "null")
				_json_leave_context()
			elseif(var MATCHES "^-?[0-9]+")
				# Number value
				_json_set_prop("${curr_field}" "${var}")
				_json_set_prop("${curr_field}/type" "number")
				_json_leave_context()
			else()
				# String value
				string(REGEX MATCH "^\"(.*)\"$" match "${var}")
				if (NOT match)
					message(FATAL_ERROR "@${curr_field}: Expected a value, but got \"${var}\"")
				endif()
				set(value "${CMAKE_MATCH_1}")
				_json_unescape_cmake_sqr_brkt(value)
				_json_set_prop("${curr_field}" "${value}")
				_json_set_prop("${curr_field}/type" "string")
				_json_leave_context()
			endif()
		elseif (${json_parse_state} STREQUAL "NAME1" OR
				${json_parse_state} STREQUAL "NAME2")
	
			if (${json_parse_state} STREQUAL "NAME1" AND
				var STREQUAL "}")
				# Object with 0 fields
				_json_leave_context()
			else()
				# Name of a field of an object
				string(REGEX MATCH "^\"(.*)\"$" match "${var}")
				if (NOT match)
					message(FATAL_ERROR "@${curr_field}: Expected a name, but got \"${var}\"")
				endif()
				string(STRIP "${CMAKE_MATCH_1}" name)
				_json_unescape_cmake_sqr_brkt(name)
				_json_enter_context("${name}")
				set(json_parse_state "NAME_SEP")
	
			endif()
		elseif (${json_parse_state} STREQUAL "FIELD_SEP")
	
			if (var STREQUAL ",")
	
				# Start of next field of an object
				set(json_parse_state "NAME2")
	
			elseif (var STREQUAL "}")
				# End of an object value
				_json_leave_context()
			else()
				message(FATAL_ERROR "@${curr_field}: Expected `,` or `}`, but got \"${var}\"")
			endif()
	
		elseif (${json_parse_state} STREQUAL "ARRAY_SEP")
	
			if (var STREQUAL ",")
	
				# increment array size
				math(EXPR n "${${curr_field}.N} + 1")
				_json_set_prop("${curr_field}.N" "${n}")
				math(EXPR n "${n} + 1")
				# Switch to next array element context
				_json_enter_context("${n}")
				set(json_parse_state "VALUE")
	
			elseif (var STREQUAL close_brace)
	
				# increment array size
				math(EXPR n "${${curr_field}.N} + 1")
				_json_set_prop("${curr_field}.N" "${n}")
				# Switch to the context of the parent of array
				_json_leave_context()
	
			else()
	
				message(FATAL_ERROR "@${curr_field}: Expected `]` or `,`, but got \"${var}\"")
	
			endif()
	
		elseif (${json_parse_state} STREQUAL "NAME_SEP")
	
			if (var STREQUAL ":")
				set(json_parse_state "VALUE")
			else()
				message(FATAL_ERROR "@${curr_field}: Expected `:`, but got \"${var}\"")
			endif()
	
		elseif (${json_parse_state} STREQUAL "END")
	
			message(FATAL_ERROR "@${curr_field}: Unexpected \"${var}\" after parsing completed")
	
		endif()
	endforeach()

	# print_json("${namespace}")

endfunction()

function(json_get_value namespace path return_value)
	cmake_parse_arguments(parsed_args "QUIET" "" "" ${ARGN})
	if (NOT parsed_args_QUIET AND NOT DEFINED "${namespace}.${path}")
		message(FATAL_ERROR "JSON path '${path}' in '${namespace}' invalid!!!")
	endif()
	set("${return_value}" "${${namespace}.${path}}" PARENT_SCOPE)
endfunction()

function(json_get_list namespace pattern return_list)
	get_cmake_property(_variableNames VARIABLES)
	string(REGEX REPLACE "\\." "\\\\." namespace_regex "${namespace}")
	list_filter_include_regex(_variableNames "^${namespace_regex}\\.${pattern}")
	#foreach (_elem IN LISTS _variableNames)
	#	string(REGEX REPLACE "^${namespace_regex}\\." "" _elem "${_elem}")
	#	list(APPEND _variableNames2 "${_elem}")
	#endforeach()
	list_transform_replace(_variableNames "^${namespace_regex}\\." "")
	set("${return_list}" "${_variableNames}" PARENT_SCOPE)
endfunction()

function(json_print_all namespace)
	get_cmake_property(_variableNames VARIABLES)
	string(REGEX REPLACE "\\." "\\\\." namespace_regex "${namespace}")
	list_filter_include_regex(_variableNames "^${namespace_regex}\\.")
	foreach (_variableName ${_variableNames})
		message("${_variableName}=${${_variableName}}")
	endforeach()
endfunction()

macro(json_set_parent_scope namespace)
	get_cmake_property(_variableNames VARIABLES)
	string(REGEX REPLACE "\\." "\\\\." namespace_regex "${namespace}")
	list_filter_include_regex(_variableNames "^${namespace_regex}(\\.|/)")
	foreach (_variableName ${_variableNames})
		set("${_variableName}" "${${_variableName}}" PARENT_SCOPE)
	endforeach()
endmacro()

macro(_json_set_prop prop value)
	set("${prop}" "${value}")
	set("${prop}" "${value}" PARENT_SCOPE)
endmacro()

function(_json_enter_context ctx)
	set("${curr_field}.${ctx}/parent" "${curr_field}" PARENT_SCOPE)
	#message("Enter:${curr_field}.${ctx}")
	set(curr_field "${curr_field}.${ctx}" PARENT_SCOPE)
endfunction()

function(_json_leave_context)
	#message("Leave:${curr_field}")
	if (NOT DEFINED "${curr_field}/parent")
		set(json_parse_state "END" PARENT_SCOPE)
		# message("END")
		return()
	endif()

	set(curr_field "${${curr_field}/parent}")
	if ("${${curr_field}/type}" STREQUAL "object")
		set(json_parse_state "FIELD_SEP" PARENT_SCOPE)
	elseif ("${${curr_field}/type}" STREQUAL "array") # Array
		set(json_parse_state "ARRAY_SEP" PARENT_SCOPE)
	else()
		message(FATAL_ERROR "@${curr_field} Assert. Script error? ${${curr_field}/type}")
	endif()
	set(curr_field "${curr_field}" PARENT_SCOPE)
endfunction()

function(_json_escape_cmake_sqr_brkt str_var)
	string(REPLACE "(" "<ROUNDOPEN>" ${str_var} "${${str_var}}")
	string(REPLACE ")" "<ROUNDCLOSE>" ${str_var} "${${str_var}}")
	string(REPLACE "[" "(" ${str_var} "${${str_var}}")
	string(REPLACE "]" ")" ${str_var} "${${str_var}}")
	set(${str_var} "${${str_var}}" PARENT_SCOPE)
endfunction()

function(_json_unescape_cmake_sqr_brkt str_var)
	string(REPLACE ")" "]" ${str_var} "${${str_var}}")
	string(REPLACE "(" "[" ${str_var} "${${str_var}}")
	string(REPLACE "<ROUNDOPEN>" "(" ${str_var} "${${str_var}}")
	string(REPLACE "<ROUNDCLOSE>" ")" ${str_var} "${${str_var}}")
	set(${str_var} "${${str_var}}" PARENT_SCOPE)
endfunction()

macro(_json_clear namespace)
	get_cmake_property(_variableNames VARIABLES)
	string(REGEX REPLACE "\\." "\\\\." namespace_regex "${namespace}")
	list_filter_include_regex(_variableNames "^${namespace_regex}\\.")
	foreach (_variableName ${_variableNames})
		unset("${_variableName}")
		unset("${_variableName}" PARENT_SCOPE)
	endforeach()
endmacro()
