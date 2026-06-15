#ifndef KICKCAT_TOOLS_KICKUI_SDO_PANEL_H
#define KICKCAT_TOOLS_KICKUI_SDO_PANEL_H

#include <memory>
#include <string>
#include <vector>

#include "BusProtocol.h"
#include "Panel.h"

namespace kickcat::kickui
{
    // Generic SDO read/write, a raw "absolute" entry, and opt-in OD discovery.
    class SdoPanel : public Panel
    {
    public:
        char const* title() const override { return "SDO & Dictionary"; }
        bool appliesTo(Device const& device) const override;
        void render(BusSession& session, Device& device) override;

    private:
        void renderOdTable(std::vector<OdObject> const& objects);

        // InputText backing buffers; sizes are generous fixed caps for the kind
        // of text each field holds (hex index, decimal/real value, raw hex dump).
        static constexpr int INDEX_BUF  = 16;
        static constexpr int VALUE_BUF  = 128;
        static constexpr int RAW_BUF    = 256;
        static constexpr int FILTER_BUF = 64;

        char index_buf_[INDEX_BUF] = "0x6041";
        int  subindex_      = 0;
        // Index into ACCESS_LABELS, sent verbatim as Bus::Access (order asserted
        // to match the enum next to the labels): 0 PARTIAL, 1 COMPLETE, 2 EMULATE.
        int  access_        = 0;
        int  type_          = 1;   // datatype index (default UNSIGNED16)
        int  last_type_     = 1;   // type used for the in-hand read result
        char value_buf_[VALUE_BUF]   = "";
        char raw_buf_[RAW_BUF]       = "";
        char filter_buf_[FILTER_BUF] = "";

        std::shared_ptr<SdoResult> last_;
        std::string                write_msg_;   // parse error for the typed value field
    };
}

#endif
