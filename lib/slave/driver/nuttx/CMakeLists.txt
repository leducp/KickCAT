
target_sources(kickcat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SPI.cc
)

target_include_directories(kickcat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kickcat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)
