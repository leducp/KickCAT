#ifndef KICKCAT_TOOLS_KICKUI_PDO_VALUES_PANEL_H
#define KICKCAT_TOOLS_KICKUI_PDO_VALUES_PANEL_H

#include "Panel.h"

namespace kickcat::kickui
{
    // Live raw process-image view: the input (TxPDO) and output (RxPDO) bytes of
    // the selected slave, refreshed each cycle while operating.
    class PdoValuesPanel : public Panel
    {
    public:
        char const* title() const override { return "PDO values"; }
        bool appliesTo(Device const& device) const override;
        void render(BusSession& session, Device& device) override;
    };
}

#endif
