#include "kickcat/PDO.h"
#include "kickcat/debug.h"
#include "protocol.h"


namespace kickcat
{
    int32_t PDO::configure()
    {
        try
        {
            auto [indexIn, pdoIn]   = esc_->findSm(SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_READ);
            auto [indexOut, pdoOut] = esc_->findSm(SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_WRITE);

            sm_input_  = SYNC_MANAGER_PI_IN(indexIn, pdoIn.start_address, pdoIn.length);
            sm_output_ = SYNC_MANAGER_PI_OUT(indexOut, pdoOut.start_address, pdoOut.length);
        }
        catch (std::exception const& e)
        {
            return -EINVAL;
        }

        return 0;
    }

    StatusCode PDO::isConfigOk()
    {
        if (not esc_->isSmValid(sm_input_))
        {
            return StatusCode::INVALID_INPUT_CONFIGURATION;
        }
        else if (not esc_->isSmValid(sm_output_))
        {
            return StatusCode::INVALID_OUTPUT_CONFIGURATION;
        }

        return StatusCode::NO_ERROR;
    }

    void PDO::activateOuput(bool is_activated)
    {
        if (sm_output_.type != SyncManagerType::Unused)
        {
            esc_->setSmActivate({sm_output_}, is_activated);
        }
    }

    void PDO::activateInput(bool is_activated)
    {
        if (sm_input_.type != SyncManagerType::Unused)
        {
            esc_->setSmActivate({sm_input_}, is_activated);
        }
    }

    void PDO::setInput(void* buffer)
    {
        input_ = buffer;
    }

    void PDO::setOutput(void* buffer)
    {
        output_ = buffer;
    }

    void PDO::updateInput()
    {
        if (not input_)
        {
            return;
        }

        std::memset(input_, 0, sm_input_.length);

        for (auto const& e : input_map_)
        {
            uint8_t* dst = static_cast<uint8_t*>(input_) + (e.bit_offset / 8);

            std::memcpy(dst, e.od_data, e.bit_len / 8);
        }

        int32_t written = esc_->write(sm_input_.start_address, input_, sm_input_.length);

        if (written != sm_input_.length)
        {
            slave_error("PDO::updateInput write error\n");
        }
    }

    void PDO::updateOutput()
    {
        if (not output_)
        {
            return;
        }

        int32_t read = esc_->read(sm_output_.start_address, output_, sm_output_.length);

        if (read != sm_output_.length)
        {
            slave_error("PDO::updateOutput read error\n");
            return;
        }

        for (auto const& e : output_map_)
        {
            uint8_t* src = static_cast<uint8_t*>(output_) + (e.bit_offset / 8);

            std::memcpy(e.od_data, src, e.bit_len / 8);
        }
    }

    std::vector<uint16_t> PDO::parseAssignment(CoE::Dictionary& dict, uint16_t assign_idx)
    {
        std::vector<uint16_t> pdo_indices;

        auto [obj0, entry0] = CoE::findObject(dict, assign_idx, 0);
        if (entry0)
        {
            uint8_t count = *static_cast<uint8_t*>(entry0->data);

            for (uint8_t i = 1; i <= count; ++i)
            {
                auto [obj, entry] = CoE::findObject(dict, assign_idx, i);
                if (entry)
                {
                    pdo_indices.push_back(*static_cast<uint16_t*>(entry->data));
                }
            }
        }

        return pdo_indices;
    }

    bool PDO::parsePdoMap(CoE::Dictionary& dict, uint16_t pdo_idx, std::vector<PdoEntry>& map, uint16_t& bit_offset)
    {
        auto [obj0, entry0] = CoE::findObject(dict, pdo_idx, 0);
        if (not entry0)
        {
            return false;
        }

        uint8_t count = *static_cast<uint8_t*>(entry0->data);

        for (uint8_t i = 1; i <= count; ++i)
        {
            auto [obj, entry] = CoE::findObject(dict, pdo_idx, i);
            if (not entry)
            {
                return false;
            }

            uint32_t mapping = *static_cast<uint32_t*>(entry->data);

            uint16_t index = (mapping >> 16) & 0xFFFF;
            uint8_t  sub   = (mapping >> 8)  & 0xFF;
            uint8_t  bits  =  mapping        & 0xFF;

            auto [od_obj, od_entry] = CoE::findObject(dict, index, sub);
            if (not od_entry)
            {
                return false;
            }

            map.push_back({
                .od_data = od_entry->data,
                .bit_len = bits,
                .bit_offset = bit_offset
            });

            bit_offset += bits;
        }

        return true;
    }

    StatusCode PDO::configureMapping(CoE::Dictionary& dict)
    {
        input_map_.clear();
        output_map_.clear();

        {
            uint16_t bit_offset = 0;
            std::vector<uint16_t> pdo_indices = parseAssignment(dict, 0x1C13);

            if (pdo_indices.empty())
            {
                return StatusCode::INVALID_INPUT_CONFIGURATION;
            }

            for (auto pdo : pdo_indices)
            {
                if (not parsePdoMap(dict, pdo, input_map_, bit_offset))
                {
                    return StatusCode::INVALID_INPUT_CONFIGURATION;
                }
            }
        }

        {
            uint16_t bit_offset = 0;
            std::vector<uint16_t> pdo_indices = parseAssignment(dict, 0x1C12);

            if (pdo_indices.empty())
            {
                return StatusCode::INVALID_OUTPUT_CONFIGURATION;
            }

            for (auto pdo : pdo_indices)
            {
                if (not parsePdoMap(dict, pdo, output_map_, bit_offset))
                {
                    return StatusCode::INVALID_OUTPUT_CONFIGURATION;
                }
            }
        }

        return StatusCode::NO_ERROR;
    }
}
