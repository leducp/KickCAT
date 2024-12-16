#ifndef SLAVE_STACK_INCLUDE_PDO_H_
#define SLAVE_STACK_INCLUDE_PDO_H_

#include <optional>
#include "AbstractESC.h"
#include "kickcat/protocol.h"

namespace kickcat
{
    class PDO final
    {
    public:
        PDO(AbstractESC* esc)
            : esc_{esc}
        {
        }

        hresult configure();
        StatusCode isConfigOk();
        void activateOuput(bool is_activated);
        void activateInput(bool is_activated);
        void setInput(void* buffer);
        void setOutput(void* buffer);
        void updateInput();
        void updateOutput();

    private:
        AbstractESC* esc_;
        void* process_data_input_                  = {nullptr};
        std::optional<SyncManagerConfig> sm_pd_input_ = {};

        void* process_data_output_                  = {nullptr};
        std::optional<SyncManagerConfig> sm_pd_output_ = {};
    };
}

#endif
