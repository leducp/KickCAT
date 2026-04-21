set(_KICKCAT_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")

if(KICKCAT_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)
endif()

function(kickcat_publish_includes target include_dir)
  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${include_dir}>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${include_dir}/kickcat)

  if(KICKCAT_INSTALL)
    install(DIRECTORY ${include_dir}/kickcat
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
      FILES_MATCHING PATTERN "*.h"
    )
  endif()
endfunction()

function(kickcat_install_package os_libraries)
  if(NOT KICKCAT_INSTALL)
    return()
  endif()

  install(TARGETS kickcat kickcat_time
    EXPORT KickCATTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  set(cmake_config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/KickCAT)

  install(EXPORT KickCATTargets
    FILE KickCATTargets.cmake
    NAMESPACE KickCAT::
    DESTINATION ${cmake_config_dir}
  )

  configure_package_config_file(
    ${_KICKCAT_CMAKE_DIR}/KickCATConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/KickCATConfig.cmake
    INSTALL_DESTINATION ${cmake_config_dir}
  )

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/KickCATConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  install(FILES
      ${CMAKE_CURRENT_BINARY_DIR}/KickCATConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/KickCATConfigVersion.cmake
    DESTINATION ${cmake_config_dir}
  )

  if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(PC_LIBDIR     "${CMAKE_INSTALL_LIBDIR}")
  else()
    set(PC_LIBDIR     "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
  endif()
  if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    set(PC_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
  else()
    set(PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
  endif()

  set(pc_requires_private "")
  set(pc_libs_private "")
  set(pc_cflags_extra "")

  set(pc_skip_libs "")
  if(ENABLE_AF_XDP AND LINUX)
    list(APPEND pc_skip_libs ${LIBXDP_LIBRARIES} ${LIBBPF_LIBRARIES})
  endif()

  foreach(lib IN LISTS os_libraries)
    if(lib IN_LIST pc_skip_libs)
      continue()
    elseif(lib STREQUAL "tinyxml2::tinyxml2")
      list(APPEND pc_requires_private "tinyxml2")
    elseif(lib STREQUAL "npcap::npcap")
      # npcap has no .pc file; Windows consumers must add it themselves.
    elseif(lib MATCHES "^-")
      list(APPEND pc_libs_private "${lib}")
    else()
      list(APPEND pc_libs_private "-l${lib}")
    endif()
  endforeach()

  if(ENABLE_AF_XDP AND LINUX)
    list(APPEND pc_requires_private "libxdp" "libbpf")
    list(APPEND pc_cflags_extra "-DKICKCAT_AF_XDP_ENABLED")
  endif()

  list(JOIN pc_requires_private " " PC_REQUIRES_PRIVATE)
  list(JOIN pc_libs_private " "     PC_LIBS_PRIVATE)
  list(JOIN pc_cflags_extra " "     PC_CFLAGS_EXTRA)
  foreach(var PC_REQUIRES_PRIVATE PC_LIBS_PRIVATE PC_CFLAGS_EXTRA)
    if(${var})
      set(${var} " ${${var}}")
    endif()
  endforeach()

  configure_file(
    ${_KICKCAT_CMAKE_DIR}/kickcat.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/kickcat.pc
    @ONLY
  )

  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kickcat.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
  )
endfunction()
