#ifndef SLAVE_STACK_INCLUDE_ABSTRACTESC_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTESC_H_

#include <cstdint>
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
        SyncManagerType type;
    };

    #define SYNC_MANAGER_PI_IN(index, address, length)  SyncManagerConfig{index, address, length, 0x20, SyncManagerType::Input}
    #define SYNC_MANAGER_PI_OUT(index, address, length) SyncManagerConfig{index, address, length, 0x64, SyncManagerType::Output}

    #define SYNC_MANAGER_MBX_IN(index, address, length)  SyncManagerConfig{index, address, length, 0x02, SyncManagerType::MailboxIn}
    #define SYNC_MANAGER_MBX_OUT(index, address, length) SyncManagerConfig{index, address, length, 0x06, SyncManagerType::MailboxOut}


    namespace mailbox::response
    {
        class Mailbox;
    }

    // Regarding the state machine, see ETG1000.6 6.4.1 AL state machine
    class AbstractESC
    {
    public:
        virtual ~AbstractESC() = default;

        virtual hresult init()                                                   = 0;
        virtual int32_t read(uint16_t address, void* data, uint16_t size)        = 0;
        virtual int32_t write(uint16_t address, void const* data, uint16_t size) = 0;

        void routine();


        // Mailbox that the ESC will try to configure before passing to preop
        void set_mailbox(mailbox::response::Mailbox* mailbox);
        void set_process_data_input(uint8_t* buffer);
        void set_process_data_output(uint8_t* buffer);
        void set_valid_output_data_received(bool are_valid_output);
        std::tuple<uint8_t, SyncManager> find_sm(uint16_t controlMode);

        void routine_init();
        void routine_preop();
        void routine_safeop();
        void routine_op();

        void set_state_on_error(State state, StatusCode error_code);
        void clear_error();

        uint16_t al_status() { return al_status_;};

        bool has_expired_watchdog() { return not (watchdog_ & 0x1); }

        void sm_activate(SyncManagerConfig const& sm);
        void sm_deactivate(SyncManagerConfig const& sm);
        void set_sm_activate(std::vector<SyncManagerConfig> const& sync_managers, bool is_activated);
    private:
        bool configure_pdo_sm();

        void update_process_data_input();
        void update_process_data_output();

        bool is_valid_sm(SyncManagerConfig const& sm_ref);
        bool are_valid_sm(std::vector<SyncManagerConfig> const& sm);

        void set_error(StatusCode code);

        /// \brief Set only the state, preserve the other fields.
        void set_al_status(State state);

        StatusCode al_status_code_ = {StatusCode::NO_ERROR};
        uint16_t al_status_ = {0};

        uint16_t al_control_ = {0};
        uint16_t watchdog_ = {0};

        uint8_t* process_data_input_ = {nullptr};
        SyncManagerConfig sm_pd_input_ = {};

        uint8_t* process_data_output_ = {nullptr};
        SyncManagerConfig sm_pd_output_ = {};

        SyncManagerConfig sm_mbx_input_  = {};
        SyncManagerConfig sm_mbx_output_ = {};

        bool are_valid_output_data_ = false;

        mailbox::response::Mailbox* mbx_;
    };

}
#endif
