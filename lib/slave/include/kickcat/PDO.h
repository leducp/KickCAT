#ifndef KICKCAT_SLAVE_PDO_H_
#define KICKCAT_SLAVE_PDO_H_

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
        void activateOutput(bool is_activated);
        void activateInput(bool is_activated);
        void setInput(void* buffer, uint32_t size);
        void setOutput(void* buffer, uint32_t size);
        void updateInput();
        void updateOutput();

        // A mapped dictionary must remain alive until the next successful mapping or until this PDO is destroyed.
        // Its entries alias the process buffers until they are remapped.
        StatusCode configureMapping(CoE::Dictionary& dict);

        // Meaningful only after configure().
        bool hasInput()  const { return sm_input_.type  != SyncManager::Unused; }
        bool hasOutput() const { return sm_output_.type != SyncManager::Unused; }

    private:

        struct MappedEntry
        {
            CoE::Entry* entry;
            uint16_t bit_offset;
            uint8_t  bits;
        };

        bool isAssigned(CoE::Dictionary& dict, uint16_t assign_idx);
        std::vector<uint16_t> parseAssignment(CoE::Dictionary& dict, uint16_t assign_idx);

        bool collectMapping(CoE::Dictionary& dict, uint16_t assign_idx, uint32_t max_size, uint32_t sm_length, std::vector<MappedEntry>& out);
        bool parsePdoMap(CoE::Dictionary& dict, uint16_t pdo_idx, uint16_t& bit_offset, uint32_t max_size, std::vector<MappedEntry>& out);
        void bindMapping(std::vector<MappedEntry> const& mapping, void* buffer);

        void releaseMapping();

        AbstractESC* esc_;
        void* input_                = {nullptr};
        uint32_t input_size_        = 0;
        SyncManagerConfig sm_input_ = {};

        void* output_                = {nullptr};
        uint32_t output_size_        = 0;
        SyncManagerConfig sm_output_ = {};

        std::vector<MappedEntry> bound_entries_;
    };
}

#endif
