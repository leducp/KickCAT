#ifndef KICKCAT_TOOLS_COMMON_GUI_APP_H
#define KICKCAT_TOOLS_COMMON_GUI_APP_H

#include <functional>
#include <string>

#include "imgui.h"

struct GLFWwindow;

namespace kickcat::gui
{
    // Owns the GLFW window + Dear ImGui context and drives the render loop.
    // Each frame a full-viewport host window is opened and the user callback is
    // invoked inside it, so callers only draw widgets and never touch GLFW.
    class GuiApp
    {
    public:
        GuiApp(std::string const& title, int width = 1280, int height = 800);
        ~GuiApp();

        GuiApp(GuiApp const&) = delete;
        GuiApp& operator=(GuiApp const&) = delete;

        // False when GLFW/window/context creation failed; main() should bail out.
        bool valid() const { return window_ != nullptr; }

        // Blocks until the window is closed, calling render() once per frame.
        void run(std::function<void()> const& render);

        void setClearColor(ImVec4 const& color) { clear_color_ = color; }
        void setHostWindowFlags(ImGuiWindowFlags flags) { host_flags_ = flags; }

        // Built-in fixed-width font, for tabular/streaming numbers that must not
        // jitter as digits change (e.g. live PDO bytes). Null if unavailable.
        ImFont* monoFont() const { return mono_font_; }

        // HiDPI UI scale (fonts + style already scaled by it). Callers must
        // multiply their own explicit pixel sizes (column widths, item widths) by
        // this, since ImGui does not scale hardcoded dimensions.
        float scale() const { return scale_; }

    private:
        GLFWwindow* window_{nullptr};
        ImFont*     mono_font_{nullptr};
        float       scale_{1.0f};
        char const* glsl_version_{nullptr};
        ImVec4 clear_color_{0.10f, 0.10f, 0.14f, 1.00f};
        ImGuiWindowFlags host_flags_{ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus};
    };
}

#endif
