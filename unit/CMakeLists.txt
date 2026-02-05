add_executable(kickcat_unit src/adler32_sum-t.cc
                            src/AbstractSPI-t.cc
                            src/bus-t.cc
                            src/debughelpers-t.cc
                            src/diagnostics-t.cc
                            src/error-t.cc
                            src/frame-t.cc
                            src/gateway-t.cc
                            src/kickcat-t.cc
                            src/link-t.cc
                            src/prints-t.cc
                            src/protocol-t.cc
                            src/Ring-t.cc
                            src/SBufQueue-t.cc
                            src/slave-t.cc
                            src/socket-t.cc
                            src/EmulatedESC-t.cc
                            src/ESM-t.cc
                            src/ESMStateOP-t.cc
                            src/ESMStateInit-t.cc
                            src/ESMStatePreOP-t.cc
                            src/ESMStateSafeOP-t.cc
                            src/Units-t.cc
                            src/CoE/protocol-t.cc
                            src/CoE/EsiParser-t.cc
                            src/CoE/OD-t.cc
                            src/mailbox/request-t.cc
                            src/mailbox/response-t.cc
                            src/Time.cc
)

# Use for ESI as a dataset for EsiParser
file(COPY ${CMAKE_SOURCE_DIR}/examples/slave/nuttx/xmc4800/boards/wdc_foot/foot.xml DESTINATION ${CMAKE_BINARY_DIR})

target_link_libraries(kickcat_unit kickcat GTest::gmock_main)
target_include_directories(kickcat_unit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_kickcat_properties(kickcat_unit)
set_target_properties(kickcat_unit
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME kickcat COMMAND kickcat_unit WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if (CODE_COVERAGE)
  set(EXCLUDE_FILES "unit/*" ".*gtest.*" "examples/*" ".*gmock.*" ".*/OS/.*" "tools/*"
                    "conan/*" "*conan2/*" "*simulation/*")
  setup_target_for_coverage_gcovr_html(
      NAME coverage
      EXECUTABLE kickcat_unit
      EXCLUDE ${EXCLUDE_FILES}
      )

  setup_target_for_coverage_gcovr_xml(
      NAME coverage_xml
      EXECUTABLE kickcat_unit
      EXCLUDE ${EXCLUDE_FILES}
      )

  setup_target_for_coverage_lcov(
      NAME coverage_lcov
      EXECUTABLE kickcat_unit
      EXCLUDE ${EXCLUDE_FILES}
      )
endif()
