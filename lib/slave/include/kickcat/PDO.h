#ifndef SLAVE_STACK_INCLUDE_PDO_H_
#define SLAVE_STACK_INCLUDE_PDO_H_

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
        void* input_                = {nullptr};
        SyncManagerConfig sm_input_ = {};

        void* output_                = {nullptr};
        SyncManagerConfig sm_output_ = {};
    };
}

#endif
