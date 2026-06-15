#include "GuiApp.h"

#include <cstdio>
#include <cstdlib>

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "bundled_fonts.h"

namespace
{
    void glfw_error_callback(int error, char const* description)
    {
        std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
    }
}

namespace kickcat::gui
{
    namespace
    {
        // Load the embedded UI font (added first, so it stays the default) and the
        // embedded monospace font, baking the glyph ranges we use (Latin +
        // punctuation/arrows/geometric symbols) so they don't render as '?'.
        // Returns the monospace ImFont* (built-in fallback if none was bundled).
        ImFont* loadBundledFonts(ImGuiIO& io, float scale)
        {
            int count = 0;
            BundledFont const* fonts = bundledFonts(&count);
            if ((fonts == nullptr) or (count == 0))
            {
                ImFontConfig d;
                d.SizePixels = 13.0f * scale;
                return io.Fonts->AddFontDefault(&d);   // no bundle: built-in default
            }

            static ImVector<ImWchar> ranges;
            if (ranges.empty())
            {
                ImFontGlyphRangesBuilder builder;
                builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
                static const ImWchar extra[] = {
                    0x2010, 0x205E,   // general punctuation: dashes, bullet, ...
                    0x2190, 0x21FF,   // arrows
                    0x2200, 0x22FF,   // math operators (incl. minus sign 0x2212)
                    0x25A0, 0x25FF,   // geometric shapes
                    0,
                };
                builder.AddRanges(extra);
                builder.BuildRanges(&ranges);
            }

            BundledFont const* ui   = nullptr;
            BundledFont const* mono = nullptr;
            for (int i = 0; i < count; ++i)
            {
                std::string name = fonts[i].name;
                if (name.find("Mono") != std::string::npos)
                {
                    if (mono == nullptr) { mono = &fonts[i]; }
                }
                else if (ui == nullptr)
                {
                    ui = &fonts[i];
                }
            }

            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false;  // data is a static array in the binary
            if (ui != nullptr)
            {
                io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(ui->data),
                                               static_cast<int>(ui->size), 16.0f * scale, &cfg, ranges.Data);
            }
            if (mono != nullptr)
            {
                ImFontConfig mcfg;
                mcfg.FontDataOwnedByAtlas = false;
                return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(mono->data),
                                                      static_cast<int>(mono->size), 14.0f * scale, &mcfg, ranges.Data);
            }
            ImFontConfig d;
            d.SizePixels = 13.0f * scale;
            return io.Fonts->AddFontDefault(&d);   // no bundled mono: built-in fallback
        }

        // UI scale for HiDPI: the primary-monitor content scale (queried before the
        // window so the window size can use it too), overridable with KICKUI_SCALE
        // for setups where the desktop reports 1.0 on a dense panel.
        float uiScale()
        {
            float xscale = 1.0f;
            float yscale = 1.0f;
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            if (monitor != nullptr)
            {
                glfwGetMonitorContentScale(monitor, &xscale, &yscale);
            }
            float scale = xscale;
            char const* env = std::getenv("KICKUI_SCALE");
            if (env != nullptr)
            {
                float s = static_cast<float>(std::atof(env));
                if (s > 0.0f) { scale = s; }
            }
            if (scale < 1.0f) { scale = 1.0f; }
            return scale;
        }
    }

    GuiApp::GuiApp(std::string const& title, int width, int height)
    {
        glfwSetErrorCallback(glfw_error_callback);
        if (not glfwInit())
        {
            return;
        }

#if defined(__APPLE__)
        glsl_version_ = "#version 150";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
        glsl_version_ = "#version 130";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

        // HiDPI: query the scale before creating the window so the default window
        // size grows with it too (otherwise the scaled UI overflows a 1280x800 box).
        float scale = uiScale();
        scale_ = scale;
        window_ = glfwCreateWindow(static_cast<int>(width * scale), static_cast<int>(height * scale),
                                   title.c_str(), nullptr, nullptr);
        if (window_ == nullptr)
        {
            glfwTerminate();
            return;
        }
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        mono_font_ = loadBundledFonts(io, scale);   // UI + monospace fonts, DPI-scaled
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(scale);

        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init(glsl_version_);
    }

    GuiApp::~GuiApp()
    {
        if (window_ == nullptr)
        {
            return;
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    void GuiApp::run(std::function<void()> const& render)
    {
        if (window_ == nullptr)
        {
            return;
        }

        while (not glfwWindowShouldClose(window_))
        {
            glfwPollEvents();

            if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED) != 0)
            {
                glfwWaitEventsTimeout(0.1);
                continue;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            auto const* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);

            ImGui::Begin("##MainWindow", nullptr, host_flags_);
            render();
            ImGui::End();

            ImGui::Render();
            int display_w = 0;
            int display_h = 0;
            glfwGetFramebufferSize(window_, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(clear_color_.x, clear_color_.y, clear_color_.z, clear_color_.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window_);
        }
    }
}
