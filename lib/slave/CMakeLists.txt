target_sources(kickcat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AbstractEmulatedEEPROM.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AbstractESC.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ESM.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ESMStates.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/PDO.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AbstractSPI.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/EEPROM/EEPROM_factory.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/ESC/EmulatedESC.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ESC/Lan9252.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ESC/XMC4800.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/slave/Slave.cc
)

kickcat_publish_includes(kickcat ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (NUTTX)
  target_sources(kickcat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/driver/src/nuttx/SPI.cc
  )

  if (NUTTX_ARCH_CHIP STREQUAL "xmc4")
    target_sources(kickcat PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/EEPROM/XMC4800EEPROM.cc
    )
  endif()

  kickcat_publish_includes(kickcat ${CMAKE_CURRENT_SOURCE_DIR}/driver/include)
endif()

if (RPI)
  target_sources(kickcat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/driver/src/rpi/SPI.cc
  )
  kickcat_publish_includes(kickcat ${CMAKE_CURRENT_SOURCE_DIR}/driver/include)
  target_link_libraries(kickcat bcm2835)
endif()
