add_executable(kickcat_unit src/adler32_sum-t.cc
                            src/AbstractSPI-t.cc
                            src/bus-t.cc
                            src/dc-t.cc
                            src/debughelpers-t.cc
                            src/diagnostics-t.cc
                            src/error-t.cc
                            src/frame-t.cc
                            src/gateway-t.cc
                            src/kickcat-t.cc
                            src/link-t.cc
                            src/prints-t.cc
                            src/protocol-t.cc
                            src/redundancy-t.cc
                            src/Ring-t.cc
                            src/SBufQueue-t.cc
                            src/SIIParser-t.cc
                            src/slave-t.cc
                            src/socket-t.cc
                            src/EEPROM_factory-t.cc
                            src/EmulatedESC-t.cc
                            src/EmulatedNetwork-t.cc
                            src/ESM-t.cc
                            src/ESMStateOP-t.cc
                            src/ESMStateInit-t.cc
                            src/ESMStatePreOP-t.cc
                            src/ESMStateSafeOP-t.cc
                            src/Units-t.cc
                            src/CoE/protocol-t.cc
                            src/CoE/OD-t.cc
                            src/ESI/Parser-t.cc
                            src/ESI/SIIBuilder-t.cc
                            src/CoE/DS402StateMachine-t.cc
                            src/CoE/DS402Drive-t.cc
                            src/mailbox/request-t.cc
                            src/mailbox/response-t.cc
                            src/mailbox/CoE/request-t.cc
                            src/mailbox/CoE/response-t.cc
                            src/masterOD-t.cc
                            src/masterOD-gateway-t.cc
                            src/slave/slave-t.cc
                            src/slave/PDO-t.cc
                            src/Time.cc
)

# ESI fixtures for Parser-t.cc, copied next to the test binary as a build
# dependency so editing a fixture re-copies it without re-running CMake.
set(KICKCAT_ESI_FIXTURES
    ${CMAKE_SOURCE_DIR}/examples/slave/nuttx/xmc4800/boards/wdc_foot/foot.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/kickcat_esi_test_basic.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/kickcat_esi_test_complex.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/kickcat_esi_test_multi_device.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/kickcat_esi_test_sm_fmmu.xml
)

set(KICKCAT_ESI_FIXTURES_COPIED "")
foreach(fixture ${KICKCAT_ESI_FIXTURES})
    get_filename_component(fixture_name ${fixture} NAME)
    add_custom_command(
        OUTPUT  ${CMAKE_BINARY_DIR}/${fixture_name}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${fixture} ${CMAKE_BINARY_DIR}/${fixture_name}
        DEPENDS ${fixture}
        COMMENT "Copying ESI fixture ${fixture_name}"
        VERBATIM)
    list(APPEND KICKCAT_ESI_FIXTURES_COPIED ${CMAKE_BINARY_DIR}/${fixture_name})
endforeach()
add_custom_target(kickcat_unit_fixtures DEPENDS ${KICKCAT_ESI_FIXTURES_COPIED})
add_dependencies(kickcat_unit kickcat_unit_fixtures)

target_link_libraries(kickcat_unit kickcat GTest::gmock_main)
target_include_directories(kickcat_unit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_kickcat_properties(kickcat_unit)
set_target_properties(kickcat_unit
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME kickcat COMMAND kickcat_unit WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if (ENABLE_CODE_COVERAGE)
  set(EXCLUDE_FILES "unit/*" ".*gtest.*" "examples/*" ".*gmock.*" ".*/OS/.*" "tools/*"
                    "conan/*" "*conan2/*" "*simulation/*" "test/*")
  setup_target_for_coverage_gcovr_html(
      NAME coverage
      EXECUTABLE $<TARGET_FILE:kickcat_unit>
      DEPENDENCIES kickcat_unit
      EXCLUDE ${EXCLUDE_FILES}
      )

  setup_target_for_coverage_gcovr_xml(
      NAME coverage_xml
      EXECUTABLE $<TARGET_FILE:kickcat_unit>
      DEPENDENCIES kickcat_unit
      EXCLUDE ${EXCLUDE_FILES}
      )

  setup_target_for_coverage_lcov(
      NAME coverage_lcov
      EXECUTABLE $<TARGET_FILE:kickcat_unit>
      DEPENDENCIES kickcat_unit
      EXCLUDE ${EXCLUDE_FILES}
      )
endif()
