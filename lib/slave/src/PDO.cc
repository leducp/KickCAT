#include "kickcat/PDO.h"
#include "kickcat/debug.h"


namespace kickcat
{
    hresult PDO::configure_pdo_sm()
    {
        try
        {
            auto [indexIn, pdoIn]   = esc_->find_sm(SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_READ);
            auto [indexOut, pdoOut] = esc_->find_sm(SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_WRITE);

            sm_pd_input_  = SYNC_MANAGER_PI_IN(indexIn, pdoIn.start_address, pdoIn.length);
            sm_pd_output_ = SYNC_MANAGER_PI_OUT(indexOut, pdoOut.start_address, pdoOut.length);
        }

        catch (std::exception const& e)
        {
            return hresult::E_EINVAL;
        }

        return hresult::OK;
    }

    bool PDO::is_sm_config_ok()
    {
        bool valid = true;
        for (auto& sm : {sm_pd_input_, sm_pd_output_})
        {
            valid &= esc_->is_valid_sm(sm);
        }
        return valid;
    }
    void PDO::set_process_data_input(uint8_t* buffer)
    {
        process_data_input_ = buffer;
    }

    void PDO::set_process_data_output(uint8_t* buffer)
    {
        process_data_output_ = buffer;
    }

    void PDO::update_process_data_input()
    {
        int32_t written = esc_->write(sm_pd_input_.start_address, process_data_input_, sm_pd_input_.length);
        if (written != sm_pd_input_.length)
        {
            slave_error("\n update_process_data_input ERROR\n");
        }
    }

    void PDO::update_process_data_output()
    {
        int32_t r = esc_->read(sm_pd_output_.start_address, process_data_output_, sm_pd_output_.length);
        if (r != sm_pd_output_.length)
        {
            slave_error("\n update_process_data_output ERROR\n");
        }
    }
}
