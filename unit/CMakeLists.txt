enable_testing()

add_executable(kickcat_unit src/adler32_sum-t.cc
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
                            src/ESM-t.cc
                            src/ESMStateOP-t.cc
                            src/ESMStateInit-t.cc
                            src/ESMStatePreOP-t.cc
                            src/ESMStateSafeOP-t.cc
                            src/CoE/protocol-t.cc
                            src/mailbox/request-t.cc
                            src/mailbox/response-t.cc
                            src/Time.cc
)


target_link_libraries(kickcat_unit kickcat GTest::gmock_main)
target_include_directories(kickcat_unit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_kickcat_properties(kickcat_unit)
add_test(NAME kickcat COMMAND kickcat_unit WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if (CODE_COVERAGE)
  include(CodeCoverage)
  append_coverage_compiler_flags()
  set(GCOVR_ADDITIONAL_ARGS --exclude-unreachable-branches --exclude-throw-branches)

  set(EXCLUDE_FILES "unit/*" ".*gtest.*" "examples/*" ".*gmock.*" ".*/OS/.*" "tools/*" "conan/*" "*conan2/*")
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
