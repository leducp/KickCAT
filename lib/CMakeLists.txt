set(KICKCAT_SOURCES 
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Error.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Time.cc
)

if (UNIX)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/UdpDiagSocket.cc
  )
  set(OS_LIBRARIES pthread rt)
elseif(PIKEOS)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/ErrorCategory.cc
  )
  set(OS_LIBRARIES )
elseif(NUTTX)
  set(OS_LIB_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/Time.cc
  )
  set(OS_LIBRARIES )
endif()

# target_sources(kickcat PRIVATE ${OS_LIB_SOURCES})
add_library(kickcat ${KICKCAT_SOURCES} ${OS_LIB_SOURCES})
target_include_directories(kickcat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kickcat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat) # Does not work with private
target_link_libraries(kickcat ${OS_LIBRARIES})
set_kickcat_properties(kickcat)

add_subdirectory(master)
add_subdirectory(slave)
