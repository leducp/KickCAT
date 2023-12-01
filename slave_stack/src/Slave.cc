#include "kickcat/Slave.h"


namespace kickcat
{
    bool is_valid_sm(AbstractESC& esc, SyncManagerConfig const& sm_ref)
    {
        auto create_sm_address = [](uint16_t reg, uint16_t sm_index)
        {
            return reg + sm_index * 8;
        };

        SyncManager sm_read;

        reportError(esc.read(create_sm_address(0x0800, sm_ref.index), &sm_read, sizeof(sm_read)));

        bool is_valid = (sm_read.start_address == sm_ref.start_address) and
                        (sm_read.length == sm_ref.length) and
                        (sm_read.control == sm_ref.control);

        printf("SM read %i: start address %x, length %u, control %x status %x, activate %x \n", sm_ref.index, sm_read.start_address, sm_read.length, sm_read.control, sm_read.status, sm_read.activate);
        return is_valid;
    }


    void reportError(hresult const& rc)
    {
        if (rc != hresult::OK)
        {
            printf("\nERROR: %s code %u\n", toString(rc), rc);
        }
    }



    Slave::Slave(AbstractESC& esc)
    : esc_(esc)
    {

    }


    void Slave::init()
    {
        reportError(esc_.init());
    }

    void Slave::set_sm_mailbox_config(std::vector<SyncManagerConfig> const& mailbox)
    {
        sm_mailbox_configs_ = mailbox;
    }


    void Slave::set_process_data_input(uint8_t* buffer, SyncManagerConfig const& config)
    {
        sm_process_data_configs_.push_back(config);
        process_data_input_ = buffer;
        sm_pd_input_ = config;
    }


    void Slave::set_process_data_output(uint8_t* buffer, SyncManagerConfig const& config)
    {
        sm_process_data_configs_.push_back(config);
        process_data_output_ = buffer;
        sm_pd_output_ = config;
    }

    void Slave::routine()
    {
        reportError(esc_.read(AL_CONTROL, &al_control_, sizeof(al_control_)));
        reportError(esc_.read(AL_STATUS, &al_status_, sizeof(al_status_)));
        bool watchdog = false;
        reportError(esc_.read(WDOG_STATUS, &watchdog, 1));

        if (al_control_ & ESM_INIT)
        {
            al_status_ = ESM_INIT;
        }

        // ETG 1000.6 Table 99 – Primitives issued by ESM to Application
        switch (al_status_)
        {
            case ESM_INIT:
            {
                routine_init();
                break;
            }

            case ESM_PRE_OP:
            {
                routine_preop();
                break;
            }

            case ESM_SAFE_OP:
            {
                routine_safeop();
                break;
            }

            case ESM_OP:
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
        reportError(esc_.write(AL_STATUS, &al_status_, sizeof(al_status_)));
        reportError(esc_.write(AL_CONTROL, &al_control_, sizeof(al_control_))); // reset Error Ind Ack

        printf("al_status %x, al_control %x \n", al_status_, al_control_);
    }


    void Slave::routine_init()
    {
        uint16_t mailbox_protocol;
        reportError(esc_.read(MAILBOX_PROTOCOL, &mailbox_protocol, sizeof(mailbox_protocol)));
        printf("Mailbox protocol %x \n", mailbox_protocol);

        if (mailbox_protocol != MailboxProtocol::None)
        {
            bool are_sm_mailbox_valid = true;
            for (auto& sm : sm_mailbox_configs_)
            {
                are_sm_mailbox_valid &= is_valid_sm(esc_, sm);
            }
        }

        // TODO AL_CONTROL device identification flash led 0x0138 RUN LED Override
        if (al_control_ & ESM_PRE_OP)
        {
            al_status_ = ESM_PRE_OP;
        }
    }


    void Slave::routine_preop()
    {
        // check process data SM
        bool are_sm_process_data_valid = true;
        for (auto& sm : sm_process_data_configs_)
        {
            are_sm_process_data_valid &= is_valid_sm(esc_, sm);
        }

        if (al_control_ & ESM_SAFE_OP)
        {
            if (are_sm_process_data_valid)
            {
                al_status_ = ESM_SAFE_OP;
            }
            else
            {
                // set error flag ?
            }
        }
    }

    void Slave::routine_safeop()
    {
        update_process_data_input();
        if (al_control_ & ESM_OP)
        {
            al_status_ = ESM_OP;
        }
    }

    void Slave::routine_op()
    {
        update_process_data_input();
        update_process_data_output();
    }


    void Slave::update_process_data_output()
    {
        hresult rc = esc_.read(sm_pd_output_.start_address, process_data_output_, sm_pd_output_.length);
        if (rc != hresult::OK)
        {
            printf("\n update_process_data_output ERROR: %s code %u\n", toString(rc), rc);
        }
    }


    void Slave::update_process_data_input()
    {
        hresult rc = esc_.write(sm_pd_input_.start_address, process_data_input_, sm_pd_input_.length);
        if (rc != hresult::OK)
        {
            printf("\n update_process_data_input ERROR: %s code %u\n", toString(rc), rc);
        }
    }
}
