
add_executable(kickui
    main.cc
    BusSession.cc
    EtherCATPanel.cc
    EventLog.cc
    MotorPanel.cc
    PdoValuesPanel.cc
    PrivilegeHelper.cc
    Profile.cc
    SdoPanel.cc
    Simulator.cc
    Theme.cc
    TopologyView.cc
)

target_link_libraries(kickui PRIVATE
    kickcat
    kickcat_imgui
    portable-file-dialogs
)

set_kickcat_properties(kickui)
set_target_properties(kickui PROPERTIES OUTPUT_NAME "kickui")

if (SKBUILD)
    install(TARGETS kickui DESTINATION ${SKBUILD_SCRIPTS_DIR})
endif()
