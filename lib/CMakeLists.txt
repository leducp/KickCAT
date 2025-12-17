set(KICKCAT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/checksum/adler32.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/Frame.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Mailbox.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/TapSocket.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SIIParser.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Time.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/OD.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/protocol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/mailbox/request.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/mailbox/response.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/EoE/protocol.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/FoE/mailbox/request.cc
)


if (NUTTX)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/NuttX/Socket.cc
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/UdpDiagSocket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Mutex.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/SharedMemory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/ConditionVariable.cc
  )
  set(OS_LIBRARIES pthread rt)
elseif (WIN32)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Windows/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Windows/SharedMemory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Mutex.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/ConditionVariable.cc
  )

  find_package(npcap CONFIG REQUIRED)
  set(OS_LIBRARIES npcap::npcap pthread)
endif()

if (ENABLE_ESI_PARSER)
  find_package(tinyxml2 CONFIG REQUIRED)
  list(APPEND KICKCAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/EsiParser.cc)
  list(APPEND OS_LIBRARIES tinyxml2::tinyxml2)
endif()

add_library(kickcat ${KICKCAT_SOURCES} ${OS_LIB_SOURCES})
target_include_directories(kickcat
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)

# Provide default implementation for since_epoch()
add_library(kickcat_time ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/SinceEpoch.cc)
target_include_directories(kickcat_time
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)

target_link_libraries(kickcat ${OS_LIBRARIES} kickcat_time)

set_kickcat_properties(kickcat)

add_subdirectory(master)
add_subdirectory(slave)
