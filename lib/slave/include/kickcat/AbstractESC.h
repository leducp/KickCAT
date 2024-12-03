#ifndef SLAVE_STACK_INCLUDE_ABSTRACTESC_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTESC_H_

#include <cstdint>
#include <vector>

#include <cstdarg>
#include "kickcat/Error.h"
#include "kickcat/protocol.h"


namespace kickcat
{
    struct SyncManagerConfig
    {
        uint8_t index;
        uint16_t start_address;
        uint16_t length;
        uint8_t control;
        SyncManagerType type;
    };

#define SYNC_MANAGER_PI_IN(index, address, length)           \
    SyncManagerConfig                                        \
    {                                                        \
        index, address, length, 0x20, SyncManagerType::Input \
    }
#define SYNC_MANAGER_PI_OUT(index, address, length)           \
    SyncManagerConfig                                         \
    {                                                         \
        index, address, length, 0x64, SyncManagerType::Output \
    }

#define SYNC_MANAGER_MBX_IN(index, address, length)              \
    SyncManagerConfig                                            \
    {                                                            \
        index, address, length, 0x02, SyncManagerType::MailboxIn \
    }
#define SYNC_MANAGER_MBX_OUT(index, address, length)              \
    SyncManagerConfig                                             \
    {                                                             \
        index, address, length, 0x06, SyncManagerType::MailboxOut \
    }


    namespace mailbox::response
    {
        class Mailbox;
    }


    class AbstractESC
    {
    public:
        AbstractESC()          = default;
        virtual ~AbstractESC() = default;

        virtual hresult init() = 0;
        virtual int32_t read(uint16_t address, void* data, uint16_t size)        = 0;
        virtual int32_t write(uint16_t address, void const* data, uint16_t size) = 0;

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

    private:
        void set_error(StatusCode code);
        void clear_error();

    };

}
#endif
