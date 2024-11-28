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
        AbstractESC()          = default;
        virtual ~AbstractESC() = default;

        virtual hresult init()                                                   = 0;
        virtual int32_t read(uint16_t address, void* data, uint16_t size)        = 0;
        virtual int32_t write(uint16_t address, void const* data, uint16_t size) = 0;

        std::tuple<uint8_t, SyncManager> find_sm(uint16_t controlMode);
        void sm_activate(SyncManagerConfig const& sm);
        void sm_deactivate(SyncManagerConfig const& sm);

        bool is_valid_sm(SyncManagerConfig const& sm_ref);
        void set_sm_activate(std::vector<SyncManagerConfig> const& sync_managers, bool is_activated);
    };

}
#endif
