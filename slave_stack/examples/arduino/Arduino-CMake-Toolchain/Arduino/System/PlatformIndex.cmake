# Copyright (c) 2020 Arduino CMake Toolchain

# No need to include this recursively
if(_PLATFORM_INDEX_INCLUDED)
	return()
endif()
set(_PLATFORM_INDEX_INCLUDED TRUE)

#******************************************************************************
# Indexing of arduino platforms. Used after indexing standard package paths
# (See PackagePathIndex.cmake)

include(Arduino/Utilities/CommonUtils)
include(Arduino/Utilities/JSONParser)
include(Arduino/System/PackagePathIndex)

#==============================================================================
# Parse the JSON files contained in the standard Arduino paths to index all
# available platforms. Not all the platforms contained in the JSON files are
# installed. The installed platforms are identified and stored in the given
# 'namespace'. 
#
# After indexing the platforms, a call to 'platforms_get_list' return all 
# the indexed (installed) arduino platforms (e.g. avr, esp8266 etc). The
# returned list contains the identifier of each platform.
#
# The platform identifier can be used (in 'pl_id' argument) in a call to
# 'platforms_get_property' to return the JSON property corresponding to
# the platform.
function(IndexArduinoPlatforms namespace)

	set(json_count 0)
	set("${namespace}/list")

	InitializeArduinoPackagePathList()

	if (EXISTS "${ARDUINO_INSTALL_PATH}/hardware/package_index_bundled.json")
		# message(STATUS "Parsing package ${ARDUINO_INSTALL_PATH}/hardware/package_index_bundled.json")
		file(READ "${ARDUINO_INSTALL_PATH}/hardware/package_index_bundled.json" json_content)
		math(EXPR json_count "${json_count} + 1")
		json_parse("${json_content}" "ard_pkg.${json_count}")
		json_set_parent_scope("ard_pkg.${json_count}")
		_platforms_find_installed("ard_pkg.${json_count}" "${namespace}" TRUE)
	endif()

	file(GLOB json_list LIST_DIRECTORIES false "${ARDUINO_PACKAGE_PATH}/package_*index.json")
	foreach (json_file IN LISTS json_list)
		# message(STATUS "Parsing package ${json_file}")
		file(READ "${json_file}" json_content)
		math(EXPR json_count "${json_count} + 1")
		json_parse("${json_content}" "ard_pkg.${json_count}")
		json_set_parent_scope("ard_pkg.${json_count}")
		_platforms_find_installed("ard_pkg.${json_count}" "${namespace}" FALSE)
	endforeach()

	# TODO platforms and override platform.local.txt from the sketchbook folder

	set("${namespace}/json_count" "${json_count}" PARENT_SCOPE)
	set("${namespace}/list" "${${namespace}/list}" PARENT_SCOPE)
	# platforms_print_properties("${namespace}")

endfunction()

#==============================================================================
# As explained in 'IndexArduinoPlatforms', this function returns all the
# installed arduino platforms. Must be called after a call to 
# 'IndexArduinoPlatforms'.
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoPlatforms'
# <return_list> [OUT]: The list of installed platforms
#
function(platforms_get_list namespace return_list)
	if (NOT DEFINED ${namespace}/list)
		message(FATAL_ERROR "Platform namespace ${namespace} not found!!!")
	endif()
	set("${return_list}" "${${namespace}/list}" PARENT_SCOPE)
endfunction()

#==============================================================================
# As explained in 'IndexArduinoPlatforms', this function returns the property
# value of the specified platform.
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoPlatforms'
# <pl_id> [IN]: platform identifier (one of the entries in the list
# returned by 'platforms_get_list'
# <prop_name> [IN]: JSON property name (rooted at the specified platform
# entry within the JSON file)
# <return_value> [OUT]: The value of the property is returned in this variable
#
function(platforms_get_property namespace pl_id prop_name return_value)
	if (NOT DEFINED "${namespace}/list")
		message(FATAL_ERROR "Platform namespace ${namespace} not found!!!")
	endif()

	# If the property starts with '/' it implies a platform property and not JSON property
	string(SUBSTRING "${prop_name}" 0 1 first_letter)
	if ("${first_letter}" STREQUAL "/")
		if (NOT DEFINED "${namespace}.${pl_id}${prop_name}")
			message(FATAL_ERROR "Platform '${pl_id}' property '${prop_name}' not found in ${namespace}!!!")
		endif()
		set("${return_value}" "${${namespace}.${pl_id}${prop_name}}" PARENT_SCOPE)
	else()
		if (NOT DEFINED "${namespace}.${pl_id}/json_namespace")
			message(FATAL_ERROR "Platform ${pl_id} not found in ${namespace}!!!")
		endif()
		set(json_namespace "${${namespace}.${pl_id}/json_namespace}")
		set(json_prefix "${${namespace}.${pl_id}/json_prefix}")
		json_get_value("${json_namespace}" "${json_prefix}.${prop_name}" _value
			${ARGN})
		set("${return_value}" "${_value}" PARENT_SCOPE)
	endif()
endfunction()

#==============================================================================
# Print all the properties of all the installed platforms
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoPlatforms'
#
function(platforms_print_properties namespace)
	# message("printing ${namespace}")
	get_cmake_property(_variableNames VARIABLES)
	string(REGEX REPLACE "\\." "\\\\." namespace_regex "${namespace}")
	list_filter_include_regex(_variableNames "^${namespace_regex}(\\.|/)")
	foreach (_variableName ${_variableNames})
		message("${_variableName}=${${_variableName}}")
	endforeach()
endfunction()

#==============================================================================
# The caller of 'IndexArduinoPlatforms' can use this function to set the scope
# of the indexed platforms to its parent context (similar to PARENT_SCOPE of
# 'set' function)
#
# Arguments:
# <namespace> [IN]: The namespace passed to 'IndexArduinoPlatforms'
#
macro(platforms_set_parent_scope namespace)
	get_cmake_property(_variableNames VARIABLES)
	string(REGEX REPLACE "\\." "\\\\." namespace_regex "${namespace}")
	list_filter_include_regex(_variableNames "^${namespace_regex}\\.")
	foreach (_variableName ${_variableNames})
		set("${_variableName}" "${${_variableName}}" PARENT_SCOPE)
	endforeach()
	foreach(json_count RANGE 1 "${${namespace}/json_count}")
		json_set_parent_scope("ard_pkg.${json_count}")
	endforeach()
	foreach(pl_id IN LISTS "${namespace}/list")
		set("${namespace}.${pl_id}/json_namespace" "${${namespace}.${pl_id}/json_namespace}" PARENT_SCOPE)
		set("${namespace}.${pl_id}/json_prefix" "${${namespace}.${pl_id}/json_prefix}" PARENT_SCOPE)
		set("${namespace}.${pl_id}/path" "${${namespace}.${pl_id}/path}" PARENT_SCOPE)
		set("${namespace}.${pl_id}/local_path" "${${namespace}.${pl_id}/local_path}" PARENT_SCOPE)
		set("${namespace}.${pl_id}/hw_path" "${${namespace}.${pl_id}/hw_path}" PARENT_SCOPE)
		set("${namespace}.${pl_id}/tool_path" "${${namespace}.${pl_id}/tool_path}" PARENT_SCOPE)
	endforeach()
	set("${namespace}/json_count" "${json_count}" PARENT_SCOPE)
	set("${namespace}/list" "${${namespace}/list}" PARENT_SCOPE)
endmacro()

#==============================================================================
# Get the platform ID given the packager and the architecture. This also can be
# used to check if the given platform exists in the namespace
function(platforms_get_id namespace pkg_name pl_arch return_id)

	if (NOT DEFINED "${namespace}/list")
		message(FATAL_ERROR "Platform namespace ${namespace} not found!!!")
	endif()

	set(pl_id "${pkg_name}.${pl_arch}")

	if (NOT DEFINED "${namespace}.${pl_id}/json_namespace")
		set("${return_id}" "" PARENT_SCOPE)
		return()
	endif()

	set("${return_id}" "${pl_id}" PARENT_SCOPE)

endfunction()

#==============================================================================
# Implementation functions (Subject to change. DO NOT USE)
#

# Iterate through all the JSON files (packages) and their platforms and
# enumerate the installed ones. The identified platforms are stored in the
# '${pl_namespace}' namespace.
macro(_platforms_find_installed json_namespace pl_namespace is_bundled)

	# root path
	if ("${is_bundled}")
		set(root_path "${ARDUINO_INSTALL_PATH}/hardware")
	else()
		set(root_path "${ARDUINO_PACKAGE_PATH}/packages")
	endif()

	json_get_value("${json_namespace}" "packages.N" num_packages)
	if (num_packages EQUAL 0)
		return()
	endif()
	foreach (pkg_idx RANGE 1 "${num_packages}")

		set(pkg "packages.${pkg_idx}")

		# Check if the package is installed
		json_get_value("${json_namespace}" "${pkg}.name" pkg_name)
		# message("Checking Arduino Package: ${root_path}/${pkg_name}")
		if (NOT IS_DIRECTORY "${root_path}/${pkg_name}")
			continue()
		endif()

		json_get_value("${json_namespace}" "${pkg}.platforms.N" num_platforms)
		if (num_platforms EQUAL 0)
			continue()
		endif()
		foreach (pl_idx RANGE 1 ${num_platforms})

			set(pl "${pkg}.platforms.${pl_idx}")
			json_get_value("${json_namespace}" "${pl}.architecture" pl_arch)
			json_get_value("${json_namespace}" "${pl}.version" pl_version)
			if ("${is_bundled}")
				set(pl_path "${root_path}/${pkg_name}/${pl_arch}")
				set(local_path "${ARDUINO_SKETCHBOOK_PATH}/hardware/${pkg_name}/${pl_arch}")
				set(hw_path "${root_path}/${pkg_name}")
				set(tool_path "${root_path}/tools/${pl_arch}")
			else()
				set(pl_path "${root_path}/${pkg_name}/hardware/${pl_arch}/${pl_version}")
				set(local_path "${ARDUINO_SKETCHBOOK_PATH}/hardware/${pkg_name}/${pl_arch}")
				set(hw_path "${root_path}/${pkg_name}/hardware/${pl_arch}")
				set(tool_path "${root_path}/{tl_packager}/tools/{tool_name}/{tool_version}")
			endif()

			# Check if the platform of the specific version is installed
			# message("Checking Arduino Platform: ${pl_path}")
			if (NOT EXISTS  "${pl_path}/boards.txt" OR NOT EXISTS  "${pl_path}/platform.txt")
				continue()
			endif()

			message(STATUS "Found Arduino Platform: ${pl_path}")

			set(pl_id "${pkg_name}.${pl_arch}")
			list(APPEND "${pl_namespace}/list" "${pl_id}")
			set("${pl_namespace}.${pl_id}/json_namespace" "${json_namespace}" PARENT_SCOPE)
			set("${pl_namespace}.${pl_id}/json_prefix" "${pl}"  PARENT_SCOPE)
			set("${pl_namespace}.${pl_id}/json_pkg" "${pkg_name}"  PARENT_SCOPE)
			set("${pl_namespace}.${pl_id}/path" "${pl_path}"  PARENT_SCOPE)
			set("${pl_namespace}.${pl_id}/local_path" "${local_path}"  PARENT_SCOPE)
			set("${pl_namespace}.${pl_id}/hw_path" "${hw_path}"  PARENT_SCOPE)
			set("${pl_namespace}.${pl_id}/tool_path" "${tool_path}"  PARENT_SCOPE)

		endforeach()
	endforeach()
endmacro()

# Set the property of the platform in both the current scope and the parent scope
macro(_platforms_set_prop prop value)
	set("${prop}" "${value}")
	set("${prop}" "${value}" PARENT_SCOPE)
endmacro()

