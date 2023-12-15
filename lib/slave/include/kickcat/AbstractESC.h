#ifndef SLAVE_STACK_INCLUDE_ABSTRACTESC_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTESC_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "kickcat/protocol.h"
#include "kickcat/Error.h"


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

    #define SYNC_MANAGER_PI_IN(index, address, length)  SyncManagerConfig{index, address, length, 0x20}
    #define SYNC_MANAGER_PI_OUT(index, address, length) SyncManagerConfig{index, address, length, 0x64}

    #define SYNC_MANAGER_MBX_IN(index, address, length)  SyncManagerConfig{index, address, length, 0x02}
    #define SYNC_MANAGER_MBX_OUT(index, address, length) SyncManagerConfig{index, address, length, 0x06}


    class AbstractESC
    {
    public:
        virtual ~AbstractESC() = default;

        virtual hresult init() = 0;
        virtual hresult read(uint16_t address, void* data, uint16_t size) = 0;
        virtual hresult write(uint16_t address, void const* data, uint16_t size) = 0;

        void routine();

        void set_mailbox_config(std::vector<SyncManagerConfig> const& mailbox);
        void set_process_data_input(uint8_t* buffer, SyncManagerConfig const& config);
        void set_process_data_output(uint8_t* buffer, SyncManagerConfig const& config);
        void set_valid_output_data_received(bool are_valid_output);

        void routine_init();
        void routine_preop();
        void routine_safeop();
        void routine_op();

        uint16_t al_status() { return al_status_;};

        private:
        void update_process_data_input();
        void update_process_data_output();
        bool is_valid_sm(SyncManagerConfig const& sm_ref);

        std::vector<SyncManagerConfig> sm_mailbox_configs_ = {};
        std::vector<SyncManagerConfig> sm_process_data_configs_ = {};

        uint16_t al_status_ = {0};
        uint16_t al_control_ = {0};

        uint8_t* process_data_input_ = {nullptr};
        SyncManagerConfig sm_pd_input_ = {};

        uint8_t* process_data_output_ = {nullptr};
        SyncManagerConfig sm_pd_output_ = {};

        bool are_valid_output_data_ = false;
    };


}
#endif
