#ifndef SLAVE_STACK_INCLUDE_PDO_H_
#define SLAVE_STACK_INCLUDE_PDO_H_

#include "AbstractESC.h"

namespace kickcat
{
    class PDO final
    {
    public:
        PDO(AbstractESC* esc)
            : esc_{esc}
        {
        }

        hresult configure_pdo_sm();
        bool is_sm_config_ok();
        void set_process_data_input(uint8_t* buffer);
        void set_process_data_output(uint8_t* buffer);
        void update_process_data_input();
        void update_process_data_output();

    private:
        AbstractESC* esc_;
        uint8_t* process_data_input_   = {nullptr};
        SyncManagerConfig sm_pd_input_ = {};

        uint8_t* process_data_output_   = {nullptr};
        SyncManagerConfig sm_pd_output_ = {};
    };
}

#endif
