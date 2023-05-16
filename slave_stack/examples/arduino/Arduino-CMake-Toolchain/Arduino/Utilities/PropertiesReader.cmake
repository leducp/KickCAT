# Copyright (c) 2020 Arduino CMake Toolchain
# Copyright (c) 2018 Arduino CMake

# Note: Some code copied from Arduino-CMake-NG project and re-written
# Thanks to the contributers of the project

# No need to include this recursively
if(_PROPERTIES_READER_INCLUDED)
	return()
endif()
set(_PROPERTIES_READER_INCLUDED TRUE)

function(properties_read properties_file namespace)

	cmake_parse_arguments(parsed_args "RESET" "" "" ${ARGN})
	if (parsed_args_RESET)
		set(_prop_list "${${namespace}/list}")
		foreach(_prop IN LISTS _prop_list)
			unset("${_prop}")
			unset("${_prop}" PARENT_SCOPE)
		endforeach()
		set("${namespace}/list")
	endif()
	file(STRINGS ${properties_file} _content)
	# message("\n${_content}")
	_properties_parse(_content "${namespace}")
	set("${namespace}/list" "${${namespace}/list}" PARENT_SCOPE)
endfunction()

function(properties_get_value namespace property_name return_value)
	cmake_parse_arguments(parsed_args "QUIET" "DEFAULT" "" ${ARGN})
	if (NOT DEFINED "${namespace}.${property_name}")
		# message("parsed_args:${parsed_args_QUIET}:${parsed_args_DEFAULT}")
		if (NOT parsed_args_QUIET AND "${parsed_args_DEFAULT}" STREQUAL "")
			message(FATAL_ERROR "Property '${property_name}' in '${namespace}' invalid!!!")
		else()
			set("${return_value}" "${parsed_args_DEFAULT}" PARENT_SCOPE)
		endif()
	else()
		set("${return_value}" "${${namespace}.${property_name}}" PARENT_SCOPE)
	endif()
	
endfunction()

function(properties_set_value namespace prop value)
	if (NOT DEFINED "${namespace}.${prop}")
		LIST(APPEND "${namespace}/list" "${prop}")
		set("${namespace}/list" "${${namespace}/list}" PARENT_SCOPE)
	endif()
	# message("${namespace}:${prop}:${value}")
	set("${namespace}.${prop}" "${value}" PARENT_SCOPE)
endfunction()

function(properties_get_list namespace pattern return_list)
	set(_prop_list "${${namespace}/list}")
	list_filter_include_regex(_prop_list "${pattern}")
	set("${return_list}" "${_prop_list}" PARENT_SCOPE)
endfunction()

function(properties_resolve_value value return_value namespace)

	set(_expanded_prop_list)
	_properties_expand_value("${value}" _result_value "${namespace}"
			_expanded_prop_list)
	set("${return_value}" "${_result_value}" PARENT_SCOPE)
	foreach(_prop IN LISTS _expanded_prop_list)
		set("${namespace}.${_prop}" "${${namespace}.${_prop}}" PARENT_SCOPE)
	endforeach()

endfunction()

function(properties_resolve_all_values namespace)
	set(_prop_list "${${namespace}/list}")
	set(_expanded_prop_list)
	foreach(_prop IN LISTS _prop_list)
		list(APPEND _expanded_prop_list ${_prop})
		# message("Resolve *** ${_prop} *** : ${${namespace}.${_prop}}")
		_properties_expand_value("${${namespace}.${_prop}}" _resolved_value "${namespace}"
			_expanded_prop_list)
		set("${namespace}.${_prop}" "${_resolved_value}")
		# message("EXPANDED ${_prop}: ${${namespace}.${_prop}}")
	endforeach()
	foreach(_prop IN LISTS _expanded_prop_list)
		set("${namespace}.${_prop}" "${${namespace}.${_prop}}" PARENT_SCOPE)
	endforeach()
endfunction()

function(properties_print_all namespace)
	set(_prop_list "${${namespace}/list}")
	foreach(_prop IN LISTS _prop_list)
		message("${_prop}=${${namespace}.${_prop}}")
	endforeach()
endfunction()

macro(properties_set_parent_scope namespace)
	set(_prop_list "${${namespace}/list}")
	foreach(_prop IN LISTS _prop_list)
		set("${namespace}.${_prop}" "${${namespace}.${_prop}}" PARENT_SCOPE)
	endforeach()
	set("${namespace}/list" "${${namespace}/list}" PARENT_SCOPE)
endmacro()

function(properties_resolve_value_env value ret_value
		ret_req_var_list ret_opt_var_list ret_all_resolved)

	string(REGEX MATCHALL "{([^}]+)}" var_list "${value}")
	list(REMOVE_DUPLICATES var_list)
	set(_ret_req_var_list)
	set(_ret_opt_var_list)
	set(_ret_all_resolved 1)

	if (var_list)
		foreach(var_str IN LISTS var_list)
			string(REGEX MATCH "{([^}]+)}" match "${var_str}")
			set(var_name "${CMAKE_MATCH_1}")
			string(MAKE_C_IDENTIFIER "${var_name}" var_id)
			string(TOUPPER "${var_id}" var_id)
			if (NOT DEFINED "${var_id}")
				list(APPEND _ret_req_var_list "${var_name}")
				if (NOT DEFINED ENV{${var_id}})
					set(_ret_all_resolved 0)
					continue()
				else()
					set(var_value "$ENV{${var_id}}")
				endif()
			else()
				list(APPEND _ret_opt_var_list "${var_name}")
				if (NOT DEFINED ENV{${var_id}})
					set(var_value "${${var_id}}")
				else()
					set(var_value "$ENV{${var_id}}")
				endif()
			endif()
			string(REPLACE "${var_str}" "${var_value}" value "${value}")
		endforeach()
	endif()

	set("${ret_value}" "${value}" PARENT_SCOPE)
	set("${ret_req_var_list}" "${_ret_req_var_list}" PARENT_SCOPE)
	set("${ret_opt_var_list}" "${_ret_opt_var_list}" PARENT_SCOPE)
	set("${ret_all_resolved}" "${_ret_all_resolved}" PARENT_SCOPE)

endfunction()

macro(_properties_parse content namespace)

	set(_last_property)
	foreach (_property IN LISTS "${content}")

		string(REGEX MATCH "^([^#=]+)=([^#]*)" match "${_property}")
		if (NOT match)
			# May be part of last string (Because CMake omits binary and splits
			# as list element
			string(REGEX MATCH "^([^#]+)" match "${_property}")
			if ("${match}" STREQUAL "")
				continue()
			endif()

			string(STRIP "${CMAKE_MATCH_1}" _last_part)
			set(_last_property "${_last_property}${_last_part}" )
			string(REGEX MATCH "^([^#=]+)=([^#]*)" match "${_last_property}")
			if (NOT match)
				continue()
			endif()
			set(_property "${_last_property}" )
		endif()

		set(_property_name "${CMAKE_MATCH_1}")
		set(_property_value "${CMAKE_MATCH_2}")

		if (NOT DEFINED "${namespace}.${_property_name}")
			LIST(APPEND "${namespace}/list" "${_property_name}")
		endif()

		set("${namespace}.${_property_name}" "${_property_value}")
		set("${namespace}.${_property_name}" "${_property_value}" PARENT_SCOPE)

		set(_last_property "${_property}")
	endforeach()

endmacro()

# Utility function to expand the embedded variables
function(_properties_expand_value value return_value namespace)

	set(_value "${value}")

	# Don't resolve empty values - There's nothing to resolve
	if ("${_value}" STREQUAL "")
		set("${return_value}" "" PARENT_SCOPE)
		return()
	endif ()

	# Set previous value to nothing so that there is at least one iteration
	set(_previous_value "")
	while(NOT "${_previous_value}" STREQUAL "${_value}")
		set(_previous_value "${_value}")

		# Set previous value to nothing so that there is at least one iteration
		set(_previous_value "")
		while(NOT "${_previous_value}" STREQUAL "${_value}")
			set(_previous_value "${_value}")

			# Get the variables list
			string(REGEX MATCHALL "{[^{}/]+}" _var_list "${_value}")
			if (NOT "${_var_list}" STREQUAL "")
				list(REMOVE_DUPLICATES _var_list)
			endif()
			foreach(_var_str IN LISTS _var_list)

				# Get the variable name
				string(REGEX MATCH "^{(.*)}$" _match "${_var_str}")
				set(_var_name "${CMAKE_MATCH_1}")

				# Check if not resolved already
				if (NOT DEFINED "/prop_int_resolved.${_var_name}")
					# If such a variable is not in the namespace, no need to resolve
					if (NOT DEFINED "${namespace}.${_var_name}")
						properties_set_value("/prop_int_unresolved" ${_var_name} "")
						continue()
					endif()

					# Temporarily resolve it to the same variable to handle recursive
					# references
					properties_set_value("/prop_int_resolved" "${_var_name}"
							"{${_var_name}}")

					# message("=> Resolve *** ${_var_name} *** : "
					#	"${${namespace}.${_var_name}}")
					_properties_expand_value("${${namespace}.${_var_name}}"
							_var_value "${namespace}")
					properties_set_value("/prop_int_resolved" "${_var_name}"
							"${_var_value}")
					# message("=> EXPANDED ${_var_name}: ${_var_value}")
				endif()

			string(REPLACE "${_var_str}" "${/prop_int_resolved.${_var_name}}"
			_value "${_value}")

			endforeach()
		endwhile()
	endwhile()

	properties_set_parent_scope("/prop_int_resolved")
	properties_set_parent_scope("/prop_int_unresolved")
	set("${return_value}" "${_value}" PARENT_SCOPE)

endfunction()
