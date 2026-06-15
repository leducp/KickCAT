#ifndef KICKCAT_TOOLS_KICKUI_ETHERCAT_PANEL_H
#define KICKCAT_TOOLS_KICKUI_ETHERCAT_PANEL_H

#include <string>
#include <vector>

#include "Panel.h"
#include "BusProtocol.h"

namespace kickcat::kickui
{
    // PDO mapping viewer (the ECAT FSM state control lives in the common strip).
    class EtherCATPanel : public Panel
    {
    public:
        char const* title() const override { return "PDO"; }
        bool appliesTo(Device const& device) const override;
        void render(BusSession& session, Device& device) override;

    private:
        void renderMappingEditor(Device& device);
        // Load the last read-back into the editor; set_objects also adopts its PDO indices.
        bool seedFromReadback(Device& device, bool set_objects);

        // Manual PDO mapping editor state (per-device instance, so it persists).
        bool                  manual_map_  = false;
        int                   rx_obj_      = 0x1600;
        int                   tx_obj_      = 0x1A00;
        std::vector<PdoEntry> edit_rx_;
        std::vector<PdoEntry> edit_tx_;
        bool                  seeded_      = false;  // pre-filled from the read-back once
        std::string           readback_msg_;         // shown when "Load read-back" has nothing yet
    };
}

#endif
