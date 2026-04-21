target_sources(kickcat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Bus.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dc.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Diagnostics.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Gateway.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/helpers.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Link.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Prints.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/MailboxSequencer.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/MasterOD.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Slave.cc
)

kickcat_publish_includes(kickcat ${CMAKE_CURRENT_SOURCE_DIR}/include)
