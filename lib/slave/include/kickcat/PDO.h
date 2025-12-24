#ifndef SLAVE_STACK_INCLUDE_PDO_H_
#define SLAVE_STACK_INCLUDE_PDO_H_

#include "AbstractESC.h"
#include "kickcat/protocol.h"
#include "kickcat/CoE/OD.h"

namespace kickcat
{
    class PDO final
    {
    public:
        PDO(AbstractESC* esc)
            : esc_{esc}
        {
        }

        int32_t configure();
        StatusCode isConfigOk();
        void activateOuput(bool is_activated);
        void activateInput(bool is_activated);
        void setInput(void* buffer);
        void setOutput(void* buffer);
        void updateInput();
        void updateOutput();

        StatusCode configureMapping(CoE::Dictionary& dict);

    private:

        struct PdoEntry
        {
            void*    od_data;   // pointer to OD entry->data
            uint16_t bit_len;   // size in bits
            uint16_t bit_offset;   // offset in PDO buffer (bits)
        };

        std::vector<uint16_t> parseAssignment(CoE::Dictionary& dict, uint16_t assign_idx);

        bool parsePdoMap(CoE::Dictionary& dict, uint16_t pdo_idx, std::vector<PdoEntry>& map, uint16_t& bit_offset);

        std::vector<PdoEntry> input_map_;
        std::vector<PdoEntry> output_map_;
            
        AbstractESC* esc_;
        void* input_                = {nullptr};
        SyncManagerConfig sm_input_ = {};

        void* output_                = {nullptr};
        SyncManagerConfig sm_output_ = {};
    };
}

#endif
