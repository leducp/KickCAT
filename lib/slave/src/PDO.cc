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
        if (input_ != nullptr)
        {
            int32_t written = esc_->write(sm_input_.start_address, input_, sm_input_.length);
            if (written != sm_input_.length)
            {
                slave_error("\n update_process_data_input ERROR\n");
            }
        }
    }

    void PDO::updateOutput()
    {
        if (output_ != nullptr)
        {
            int32_t r = esc_->read(sm_output_.start_address, output_, sm_output_.length);
            if (r != sm_output_.length)
            {
                slave_error("\n update_process_data_output ERROR\n");
            }
        }
    }
}
