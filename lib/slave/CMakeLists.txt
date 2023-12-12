
target_sources(kickcat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AbstractESC.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AbstractSPI.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ESC/Lan9252.cc
)

target_include_directories(kickcat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kickcat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)

if (NUTTX)
  add_subdirectory(driver/nuttx)
endif()
