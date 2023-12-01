
#ifndef SLAVE_STACK_INCLUDE_KICKCAT_SLAVE_H_
#define SLAVE_STACK_INCLUDE_KICKCAT_SLAVE_H_

#include "kickcat/AbstractESC.h"


#include "kickcat/ESC/Lan9252.h" // TODO remove when access to Protocol.h

#include <vector>

namespace kickcat
{

    // TODO define to a proper place
    void reportError(hresult const& rc);

    struct SyncManagerConfig
    {
        uint8_t index;
        uint16_t start_address;
        uint16_t length;
        uint8_t  control;
    };

    bool is_valid_sm(AbstractESC& esc, SyncManagerConfig const& sm_ref);

    class Slave
    {
    public:
        Slave(AbstractESC& esc);
        ~Slave() = default;

        void init();

        void routine();

        void set_sm_mailbox_config(std::vector<SyncManagerConfig> const& mailbox);

        void set_process_data_input(uint8_t* buffer, SyncManagerConfig const& config);
        void set_process_data_output(uint8_t* buffer, SyncManagerConfig const& config);

        void routine_init();
        void routine_preop();
        void routine_safeop();
        void routine_op();

    private:
        void update_process_data_input();
        void update_process_data_output();

        AbstractESC& esc_;

        std::vector<SyncManagerConfig> sm_mailbox_configs_ = {};
        std::vector<SyncManagerConfig> sm_process_data_configs_ = {};

        uint16_t al_status_ = {0};
        uint16_t al_control_ = {0};

        uint8_t* process_data_input_ = {nullptr};
        SyncManagerConfig sm_pd_input_ = {};

        uint8_t* process_data_output_ = {nullptr};
        SyncManagerConfig sm_pd_output_ = {};
    };



}
#endif
