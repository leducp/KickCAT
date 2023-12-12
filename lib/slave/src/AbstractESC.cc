#include "kickcat/AbstractESC.h"

#include <inttypes.h>

namespace kickcat
{
    void reportError(hresult const& rc)
    {
        if (rc != hresult::OK)
        {
            printf("\nERROR: %s code %" PRIu32 "\n", toString(rc), static_cast<uint32_t>(rc));
        }
    }


    bool AbstractESC::is_valid_sm(SyncManagerConfig const& sm_ref)
    {
        auto create_sm_address = [](uint16_t reg, uint16_t sm_index)
        {
            return reg + sm_index * 8;
        };

        SyncManager sm_read;

        reportError(read(create_sm_address(0x0800, sm_ref.index), &sm_read, sizeof(sm_read)));

        bool is_valid = (sm_read.start_address == sm_ref.start_address) and
                        (sm_read.length == sm_ref.length) and
                        (sm_read.control == sm_ref.control);

        printf("SM read %i: start address %x, length %u, control %x status %x, activate %x \n", sm_ref.index, sm_read.start_address, sm_read.length, sm_read.control, sm_read.status, sm_read.activate);
        return is_valid;
    }


    void AbstractESC::set_mailbox_config(std::vector<SyncManagerConfig> const& mailbox)
    {
        sm_mailbox_configs_ = mailbox;
    }


    void AbstractESC::set_process_data_input(uint8_t* buffer, SyncManagerConfig const& config)
    {
        sm_process_data_configs_.push_back(config);
        process_data_input_ = buffer;
        sm_pd_input_ = config;
    }


    void AbstractESC::set_process_data_output(uint8_t* buffer, SyncManagerConfig const& config)
    {
        sm_process_data_configs_.push_back(config);
        process_data_output_ = buffer;
        sm_pd_output_ = config;
    }


    void AbstractESC::routine()
    {
        reportError(read(reg::AL_CONTROL, &al_control_, sizeof(al_control_)));
        reportError(read(reg::AL_STATUS, &al_status_, sizeof(al_status_)));
        bool watchdog = false;
        reportError(read(reg::WDOG_STATUS, &watchdog, 1));

        if (al_control_ & State::INIT)
        {
            al_status_ = State::INIT;
        }

        // ETG 1000.6 Table 99 – Primitives issued by ESM to Application
        switch (al_status_)
        {
            case State::INIT:
            {
                routine_init();
                break;
            }

            case State::PRE_OP:
            {
                routine_preop();
                break;
            }

            case State::SAFE_OP:
            {
                routine_safeop();
                break;
            }

            case State::OPERATIONAL:
            {
                routine_op();
                break;
            }
            default:
            {
                printf("Unknown or error al_status %x \n", al_status_);
            }
        }

        al_control_ &= ~AL_CONTROL_ERR_ACK;
        reportError(write(reg::AL_STATUS, &al_status_, sizeof(al_status_)));
        reportError(write(reg::AL_CONTROL, &al_control_, sizeof(al_control_))); // reset Error Ind Ack

        printf("al_status %x, al_control %x \n", al_status_, al_control_);
    }


    void AbstractESC::routine_init()
    {
        // TODO AL_CONTROL device identification flash led 0x0138 RUN LED Override
        if (al_control_ & State::PRE_OP)
        {
            uint16_t mailbox_protocol;
            reportError(read(reg::MAILBOX_PROTOCOL, &mailbox_protocol, sizeof(mailbox_protocol)));
            printf("Mailbox protocol %x \n", mailbox_protocol);

            bool are_sm_mailbox_valid = true;
            if (mailbox_protocol != mailbox::Type::None)
            {
                for (auto& sm : sm_mailbox_configs_)
                {
                    are_sm_mailbox_valid &= is_valid_sm(sm);
                }
            }

            if (are_sm_mailbox_valid)
            {
                al_status_ = State::PRE_OP;
            }
            else
            {
                // TODO error flag
            }
        }
    }


    void AbstractESC::routine_preop()
    {
        update_process_data_input();
        if (al_control_ & State::SAFE_OP)
        {
            // check process data SM
            bool are_sm_process_data_valid = true;
            for (auto& sm : sm_process_data_configs_)
            {
                are_sm_process_data_valid &= is_valid_sm(sm);
            }

            if (are_sm_process_data_valid)
            {
                al_status_ = State::SAFE_OP;
            }
            else
            {
                //TODO set error flag / report error to master
            }
        }
    }


    void AbstractESC::routine_safeop()
    {
        update_process_data_input();
        update_process_data_output();

        if (al_control_ & State::OPERATIONAL)
        {
            if (are_valid_output_data_)
            {
                al_status_ = State::OPERATIONAL;
            }
            else
            {
                // error flag
            }
        }
    }


    void AbstractESC::routine_op()
    {
        update_process_data_input();
        update_process_data_output();
    }


    void AbstractESC::update_process_data_output()
    {
        hresult rc = read(sm_pd_output_.start_address, process_data_output_, sm_pd_output_.length);
        if (rc != hresult::OK)
        {
            printf("\n update_process_data_output ERROR: %s code %" PRIu32"\n", toString(rc), static_cast<uint32_t>(rc));
        }
    }


    void AbstractESC::update_process_data_input()
    {
        hresult rc = write(sm_pd_input_.start_address, process_data_input_, sm_pd_input_.length);
        if (rc != hresult::OK)
        {
            printf("\n update_process_data_input ERROR: %s code %" PRIu32"\n", toString(rc), static_cast<uint32_t>(rc));
        }
    }


    void AbstractESC::set_valid_output_data_received(bool are_valid_output)
    {
        printf("Set valid output data received \n");
        are_valid_output_data_ = are_valid_output;
    }
}
