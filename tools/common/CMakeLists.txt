find_package(imgui REQUIRED CONFIG)
find_package(OpenGL REQUIRED)

# GLFW via FetchContent — the Conan glfw recipe pulls xorg/system which
# demands dozens of X11 -dev packages that are not actually needed.
include(FetchContent)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# ImGui backend + helper sources shipped in the Conan package under res/
get_filename_component(_IMGUI_PKG_DIR "${imgui_INCLUDE_DIR}" DIRECTORY)
set(IMGUI_RES_DIR "${_IMGUI_PKG_DIR}/res")
set(IMGUI_BACKENDS_DIR "${IMGUI_RES_DIR}/bindings")

# Embed the bundled fonts (assets/fonts/*.ttf) into a generated source file.
file(GLOB KICKCAT_BUNDLED_FONTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.otf"
)
set(KICKCAT_BUNDLED_FONTS_CC "${CMAKE_CURRENT_BINARY_DIR}/bundled_fonts.cc")
add_custom_command(
    OUTPUT  "${KICKCAT_BUNDLED_FONTS_CC}"
    COMMAND ${CMAKE_COMMAND}
            -DOUTPUT=${KICKCAT_BUNDLED_FONTS_CC}
            "-DFONTS=${KICKCAT_BUNDLED_FONTS}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_fonts.cmake"
    DEPENDS ${KICKCAT_BUNDLED_FONTS}
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_fonts.cmake"
    VERBATIM
)

# Shared GLFW/OpenGL3 ImGui setup reused by every KickCAT GUI tool.
add_library(kickcat_imgui STATIC
    GuiApp.cc
    ${KICKCAT_BUNDLED_FONTS_CC}
    ${IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_opengl3.cpp
    ${IMGUI_RES_DIR}/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(kickcat_imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_BACKENDS_DIR}
    ${IMGUI_RES_DIR}
)

target_link_libraries(kickcat_imgui PUBLIC
    imgui::imgui
    glfw
    OpenGL::GL
)

set_kickcat_properties(kickcat_imgui)
