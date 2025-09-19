#include "kickcat/AbstractESC.h"
#include "Error.h"

namespace kickcat
{
    std::tuple<uint8_t, SyncManager> AbstractESC::findSm(uint16_t controlMode)
    {
        for (uint8_t i = 0; i < reg::SM_STATS; i++)
        {
            SyncManager sync{};
            read(reg::SYNC_MANAGER + sizeof(SyncManager) * i, &sync, sizeof(SyncManager));
            if ((sync.control & 0x0F) == (controlMode & 0x0F))
            {
                return std::tuple(i, sync);
            }
        }

        THROW_ERROR("SyncManager not found");
    }

    bool AbstractESC::isSmValid(SyncManagerConfig const& sm_ref)
    {
        SyncManager sm_read;
        read(addressSM(sm_ref.index), &sm_read, sizeof(sm_read));

        bool is_valid = (sm_read.start_address == sm_ref.start_address) and (sm_read.length == sm_ref.length)
                        and ((sm_read.control & SYNC_MANAGER_CONTROL_OPERATION_MODE_MASK)
                             == (sm_ref.control & SYNC_MANAGER_CONTROL_OPERATION_MODE_MASK))
                        and ((sm_read.control & SYNC_MANAGER_CONTROL_DIRECTION_MASK)
                             == (sm_ref.control & SYNC_MANAGER_CONTROL_DIRECTION_MASK))
                        and (sm_read.activate & SM_ACTIVATE_ENABLE);

        // printf("SM read %i: start address %x, length %u, control %x, status %x, activate %x \n", sm_ref.index, sm_read.start_address, sm_read.length, sm_read.control, sm_read.status, sm_read.activate);
        // printf("SM config %i: start address %x, length %u, control %x \n", sm_ref.index, sm_ref.start_address, sm_ref.length, sm_ref.control);
        return is_valid;
    }

    void AbstractESC::activateSm(SyncManagerConfig const& sm_conf)
    {
        auto const address = addressSM(sm_conf.index) + 7;

        uint8_t pdi_control;
        read(address, &pdi_control, sizeof(uint8_t));

        uint8_t sm_deactivated = 0x1;
        pdi_control &= ~sm_deactivated;
        write(address, &pdi_control, sizeof(uint8_t));

        do
        {
            read(address, &pdi_control, 1);
        } while ((pdi_control & 1) == 1);
    }

    void AbstractESC::deactivateSm(SyncManagerConfig const& sm_conf)
    {
        auto const address = addressSM(sm_conf.index) + 7;

        uint8_t pdi_control;
        read(address, &pdi_control, sizeof(uint8_t));

        uint8_t sm_deactivated = 0x1;
        pdi_control |= sm_deactivated;
        write(address, &pdi_control, sizeof(uint8_t));

        while (true)
        {
            read(address, &pdi_control, sizeof(uint8_t));
            if ((pdi_control & 1) == 1)
            {
                break;
            }
        }
    }

    void AbstractESC::setSmActivate(std::vector<SyncManagerConfig> const& sync_managers, bool is_activated)
    {
        for (auto& sm_conf : sync_managers)
        {
            if (is_activated)
            {
                activateSm(sm_conf);
            }
            else
            {
                deactivateSm(sm_conf);
            }
        }
    }
}
