#ifndef SLAVE_STACK_INCLUDE_ABSTRACTESC_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTESC_H_

#include <cstdint>
#include <vector>

#include "kickcat/Error.h"
#include "kickcat/protocol.h"


namespace kickcat
{
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
        AbstractESC()          = default;
        virtual ~AbstractESC() = default;

        /// \return 0 if success, a negative errno otherwise
        virtual int32_t init() { return 0; }

        /// \param  address Address to read to
        /// \param  data    Buffer to store the data to
        /// \param  size    Size of the data to read
        /// \return Number of bytes read or a negative errno code otherwise
        virtual int32_t read(uint16_t address, void* data, uint16_t size)        = 0;

        /// \param  address Address to write to
        /// \param  data    Buffer that contains the data to write
        /// \param  size    Size of the data to write
        /// \return Number of bytes written or a negative errno code otherwise
        virtual int32_t write(uint16_t address, void const* data, uint16_t size) = 0;

        std::tuple<uint8_t, SyncManager> findSm(uint16_t controlMode);
        void activateSm(SyncManagerConfig const& sm);
        void deactivateSm(SyncManagerConfig const& sm);

        bool isSmValid(SyncManagerConfig const& sm_ref);
        void setSmActivate(std::vector<SyncManagerConfig> const& sync_managers, bool is_activated);
    };

}
#endif
