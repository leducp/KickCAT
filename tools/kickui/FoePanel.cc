#include "FoePanel.h"

#include <cstdio>
#include <string>
#include <vector>

#include <portable-file-dialogs.h>

#include "imgui.h"

#include "kickcat/OS/Filesystem.h"

#include "BusSession.h"
#include "Theme.h"

namespace kickcat::kickui
{
    namespace
    {
        std::string kib(uint32_t bytes)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f KiB", static_cast<double>(bytes) / 1024.0);
            return text;
        }
    }

    bool FoePanel::appliesTo(Device const& device) const
    {
        return device.has_foe;
    }

    void FoePanel::render(BusSession& session, Device& device)
    {
        if (not session.foeAvailable())
        {
            if (session.isOperatingAny())
            {
                ImGui::TextDisabled("Go back to PRE-OP to transfer files.");
            }
            else
            {
                ImGui::TextDisabled("Connect to a bus to transfer files.");
            }
            return;
        }

        bool busy = session.foeTransfer(device.index).running;

        ImGui::SeparatorText("File transfer");
        ImGui::SetNextItemWidth(px(260.0f));
        ImGui::InputTextWithHint("Remote name", "default: the local file name", remote_name_, sizeof(remote_name_));
        ImGui::SetNextItemWidth(px(120.0f));
        ImGui::InputScalar("Password", ImGuiDataType_U32, &password_, nullptr, nullptr, "%08X",
                           ImGuiInputTextFlags_CharsHexadecimal);

        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Push file..."))
        {
            auto selection = pfd::open_file("File to send to the slave", ".").result();
            if (not selection.empty())
            {
                std::string path = selection[0];
                std::string name = remote_name_;
                if (name.empty())
                {
                    name = filesystem::filename(path);
                }
                try
                {
                    std::vector<uint8_t> file = filesystem::readFile(path);
                    message_.clear();
                    started_    = true;
                    pull_       = false;
                    cancelling_ = false;
                    session.writeFoE(device.index, name, password_, std::move(file));
                }
                catch (std::exception const& e)
                {
                    message_ = "Cannot read " + path + ": " + e.what();
                }
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(remote_name_[0] == '\0');
        if (ImGui::Button("Pull file..."))
        {
            std::string path = pfd::save_file("Save the slave file as", remote_name_).result();
            if (not path.empty())
            {
                message_.clear();
                started_    = true;
                pull_       = true;
                saved_      = false;
                cancelling_ = false;
                save_path_  = path;
                session.readFoE(device.index, remote_name_, password_);
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        renderTransfer(session, device.index);
    }

    void FoePanel::renderTransfer(BusSession& session, int slave_index)
    {
        if (not message_.empty())
        {
            ImGui::TextColored(COLOR_RED, "%s", message_.c_str());
            return;
        }
        if (not started_)
        {
            return;
        }

        FoeTransfer transfer = session.foeTransfer(slave_index);
        if (transfer.running)
        {
            if (transfer.total != 0)
            {
                std::string label = kib(transfer.transferred) + " / " + kib(transfer.total);
                ImGui::ProgressBar(static_cast<float>(transfer.transferred) / static_cast<float>(transfer.total),
                                   ImVec2(-px(90.0f), 0.0f), label.c_str());
            }
            else
            {
                std::string label = kib(transfer.transferred);
                ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-px(90.0f), 0.0f), label.c_str());
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(cancelling_);
            if (ImGui::Button("Cancel"))
            {
                cancelling_ = true;
                session.cancelFoE(slave_index);
            }
            ImGui::EndDisabled();
            return;
        }

        if (not transfer.error.empty())
        {
            ImGui::TextColored(COLOR_RED, "%s", transfer.error.c_str());
            return;
        }

        if (pull_ and not saved_)
        {
            saved_ = true;
            std::vector<uint8_t> file = session.takeFoeFile(slave_index);
            try
            {
                filesystem::writeFile(save_path_, file.data(), file.size());
            }
            catch (std::exception const& e)
            {
                message_ = "Cannot write " + save_path_ + ": " + e.what();
                return;
            }
        }

        std::string done = "OK (" + std::to_string(transfer.transferred) + " bytes)";
        if (pull_)
        {
            ImGui::TextColored(COLOR_GREEN, "%s, saved to %s", done.c_str(), save_path_.c_str());
        }
        else
        {
            ImGui::TextColored(COLOR_GREEN, "%s", done.c_str());
        }
    }
}
