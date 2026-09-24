#ifndef KICKCAT_TOOLS_KICKUI_FOE_PANEL_H
#define KICKCAT_TOOLS_KICKUI_FOE_PANEL_H

#include <cstdint>
#include <string>

#include "Panel.h"

namespace kickcat::kickui
{
    // FoE file transfer: push a local file to the slave or pull one from it.
    class FoePanel : public Panel
    {
    public:
        char const* title() const override { return "Files (FoE)"; }
        bool appliesTo(Device const& device) const override;
        void render(BusSession& session, Device& device) override;

    private:
        void renderTransfer(BusSession& session, int slave_index);

        static constexpr int NAME_BUF = 128;

        char     remote_name_[NAME_BUF] = "";
        uint32_t password_ = 0;

        bool        started_    = false;   // a transfer was started from this panel: show its outcome
        bool        pull_       = false;
        bool        saved_      = false;   // pull: the file has been written to save_path_
        bool        cancelling_ = false;
        std::string save_path_;
        std::string message_;              // local error (file I/O), shown in place of the transfer outcome
    };
}

#endif
