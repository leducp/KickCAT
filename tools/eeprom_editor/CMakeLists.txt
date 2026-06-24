
add_executable(eeprom_editor
    main.cc
    App.cc
    EditorUtils.cc
    InfoEditor.cc
    StringsEditor.cc
    GeneralEditor.cc
    SyncManagerEditor.cc
    FMMUEditor.cc
    PDOEditor.cc
)

target_link_libraries(eeprom_editor PRIVATE
    kickcat
    kickcat_imgui
)

set_kickcat_properties(eeprom_editor)
set_target_properties(eeprom_editor PROPERTIES OUTPUT_NAME "kickcat_eeprom_editor")

if (SKBUILD)
    install(TARGETS eeprom_editor DESTINATION ${SKBUILD_SCRIPTS_DIR})
endif()
