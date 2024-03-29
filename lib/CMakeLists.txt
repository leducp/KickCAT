set(KICKCAT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/checksum/adler32.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Error.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Mailbox.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Time.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/OD.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/mailbox/request.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/mailbox/response.cc
)

find_package(tinyxml2 CONFIG QUIET)
if (tinyxml2_FOUND)
  list(APPEND KICKCAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/EsiParser.cc)
endif()

if (NUTTX)
  set(OS_LIB_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/Time.cc
  )
  set(OS_LIBRARIES )
elseif(PIKEOS)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/ErrorCategory.cc
  )
  set(OS_LIBRARIES )
elseif (UNIX)
set(OS_LIB_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/Socket.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/Time.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/UdpDiagSocket.cc
)
set(OS_LIBRARIES pthread rt)
endif()

add_library(kickcat ${KICKCAT_SOURCES} ${OS_LIB_SOURCES})
target_include_directories(kickcat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)
target_link_libraries(kickcat ${OS_LIBRARIES})
if (tinyxml2_FOUND)
  target_link_libraries(kickcat tinyxml2::tinyxml2)
endif()
set_kickcat_properties(kickcat)

add_subdirectory(master)
add_subdirectory(slave)
