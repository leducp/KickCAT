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
        void setInput(void* buffer, uint32_t size);
        void setOutput(void* buffer, uint32_t size);
        void updateInput();
        void updateOutput();

        StatusCode configureMapping(CoE::Dictionary& dict);

    private:

        std::vector<uint16_t> parseAssignment(CoE::Dictionary& dict, uint16_t assign_idx);

        bool parsePdoMap(CoE::Dictionary& dict, uint16_t pdo_idx, void* buffer, uint16_t& bit_offset, uint32_t max_size);

        AbstractESC* esc_;
        void* input_                = {nullptr};
        uint32_t input_size_        = 0;
        SyncManagerConfig sm_input_ = {};

        void* output_                = {nullptr};
        uint32_t output_size_        = 0;
        SyncManagerConfig sm_output_ = {};
    };
}

#endif
