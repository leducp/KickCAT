#include "kickcat/PDO.h"
#include "kickcat/debug.h"
#include "kickcat/CoE/protocol.h"
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

    void PDO::setInput(void* buffer, uint32_t size)
    {
        input_ = buffer;
        input_size_ = size;
    }

    void PDO::setOutput(void* buffer, uint32_t size)
    {
        output_ = buffer;
        output_size_ = size;
    }

    void PDO::updateInput()
    {
        if (input_ == nullptr)
        {
            return;
        }

        int32_t written = esc_->write(sm_input_.start_address, input_, sm_input_.length);

        if (written != sm_input_.length)
        {
            slave_error("PDO::updateInput write error\n");
        }
    }

    void PDO::updateOutput()
    {
        if (output_ == nullptr)
        {
            return;
        }

        int32_t read = esc_->read(sm_output_.start_address, output_, sm_output_.length);

        if (read != sm_output_.length)
        {
            slave_error("PDO::updateOutput read error\n");
            return;
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

    bool PDO::parsePdoMap(CoE::Dictionary& dict, uint16_t pdo_idx, void* buffer, uint16_t& bit_offset, uint32_t max_size)
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

            uint16_t index = static_cast<uint16_t>((mapping & CoE::PDO::MAPPING_INDEX_MASK) >> CoE::PDO::MAPPING_INDEX_SHIFT);
            uint8_t  sub   = static_cast<uint8_t>((mapping & CoE::PDO::MAPPING_SUB_MASK) >> CoE::PDO::MAPPING_SUB_SHIFT);
            uint8_t  bits  = static_cast<uint8_t>(mapping & CoE::PDO::MAPPING_LENGTH_MASK);

            if (max_size > 0 and static_cast<uint32_t>((bit_offset + bits + 7) / 8) > max_size)
            {
                slave_error("PDO::parsePdoMap mapping size exceeds buffer size\n");
                return false;
            }

            auto [od_obj, od_entry] = CoE::findObject(dict, index, sub);
            if (not od_entry)
            {
                return false;
            }

            // Aliasing logic
            void* old_data = od_entry->data;
            bool old_is_mapped = od_entry->is_mapped;

            uint8_t* new_ptr = static_cast<uint8_t*>(buffer) + (bit_offset / 8);

            od_entry->data = new_ptr;
            od_entry->is_mapped = true; // data has been remapped/aliased

            if (old_data)
            {
                std::memcpy(new_ptr, old_data, bits / 8);

                if (not old_is_mapped) // if the old data was not mapped, we allocated it, so free it
                {
                    std::free(old_data);
                }
            }

            bit_offset += bits;
        }

        return true;
    }

    StatusCode PDO::configureMapping(CoE::Dictionary& dict)
    {
        {
            uint16_t bit_offset = 0;
            std::vector<uint16_t> pdo_indices = parseAssignment(dict, 0x1C13);

            for (auto pdo : pdo_indices)
            {
                if (not parsePdoMap(dict, pdo, input_, bit_offset, input_size_))
                {
                    return StatusCode::INVALID_INPUT_CONFIGURATION;
                }
            }
        }

        {
            uint16_t bit_offset = 0;
            std::vector<uint16_t> pdo_indices = parseAssignment(dict, 0x1C12);

            for (auto pdo : pdo_indices)
            {
                if (not parsePdoMap(dict, pdo, output_, bit_offset, output_size_))
                {
                    return StatusCode::INVALID_OUTPUT_CONFIGURATION;
                }
            }
        }

        return StatusCode::NO_ERROR;
    }
}
