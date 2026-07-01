find_package(imgui REQUIRED CONFIG)

add_library(imgui_stdlib STATIC
    imgui_stdlib.cpp
)

target_include_directories(imgui_stdlib
    PUBLIC
    .
)

target_link_libraries(imgui_stdlib PUBLIC
    imgui::imgui
)
