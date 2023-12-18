target_sources(kickcat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AbstractESC.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AbstractSPI.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ESC/Lan9252.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ESC/XMC4800.cc
)

target_include_directories(kickcat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)

if (NUTTX)
  target_sources(kickcat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/driver/nuttx/src/SPI.cc
  )

  target_include_directories(kickcat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/driver/nuttx/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}driver/nuttx//include/kickcat/nuttx)
endif()
