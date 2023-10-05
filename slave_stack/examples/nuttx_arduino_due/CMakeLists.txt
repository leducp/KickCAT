cmake_minimum_required(VERSION 3.0.0)

project(easycat_slave CXX)

set(STACK_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/nuttx/SPI.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/AbstractSPI.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/Error.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ESC/Lan9252.cc
   )

add_executable(main main.cc ${STACK_LIB_SOURCES})

target_include_directories(main PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/
                                        ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
