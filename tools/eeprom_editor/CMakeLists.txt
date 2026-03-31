find_package(imgui REQUIRED CONFIG)
find_package(OpenGL REQUIRED)
find_package(portable-file-dialogs REQUIRED CONFIG)

# GLFW via FetchContent — the Conan glfw recipe pulls xorg/system which
# demands dozens of X11 -dev packages that are not actually needed.
include(FetchContent)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# ImGui backend sources shipped in the Conan package under res/bindings/
get_filename_component(_IMGUI_PKG_DIR "${imgui_INCLUDE_DIR}" DIRECTORY)
set(IMGUI_RES_DIR "${_IMGUI_PKG_DIR}/res")
set(IMGUI_BACKENDS_DIR "${IMGUI_RES_DIR}/bindings")

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
    ${IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_opengl3.cpp
    ${IMGUI_RES_DIR}/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(eeprom_editor PRIVATE
    ${IMGUI_BACKENDS_DIR}
    ${IMGUI_RES_DIR}
)

target_link_libraries(eeprom_editor PRIVATE
    kickcat
    imgui::imgui
    glfw
    OpenGL::GL
    portable-file-dialogs::portable-file-dialogs
)

set_kickcat_properties(eeprom_editor)
