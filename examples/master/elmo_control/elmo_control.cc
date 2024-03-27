#include "kickcat/Link.h"

#include "kickcat/SocketNull.h"

#include "kickcat/Mailbox.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <iostream>

using namespace kickcat;

int main(int argc, char* argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
    }


    std::shared_ptr<AbstractSocket> socket_redundancy;
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[1];

    if (argc == 2)
    {
        printf("No redundancy mode selected \n");
        socket_redundancy = std::make_shared<SocketNull>();
    }
    else
    {
        socket_redundancy = std::make_shared<Socket>();
        red_interface_name = argv[2];
    }

    auto socket_nominal = std::make_shared<Socket>();
    try
    {
        socket_nominal->open(nom_interface_name);
        socket_redundancy->open(red_interface_name);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto report_redundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link= std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(2ms);

    auto process = [](DatagramHeader const*, uint8_t const*, uint16_t wkc)
    {
        if (wkc != 1)
        {
            return DatagramState::INVALID_WKC;
        }
        return DatagramState::OK;
    };

    auto error = [](DatagramState const&)
    {
        printf("Invalid working counter \n");
    };

    uint16_t slave_address = 0x0;

// Init SM
    SyncManager SM[2];
    mailbox::request::Mailbox mailbox;
    mailbox.recv_size = 128;
    mailbox.recv_offset = 0x1000;
    mailbox.send_size = 128;
    mailbox.send_offset = 0x1400;
    mailbox.generateSMConfig(SM);
    link->addDatagram(Command::FPWR, createAddress(slave_address, reg::SYNC_MANAGER), SM, process, error);
    link->processDatagrams();

    auto process_write = [](DatagramHeader const*, uint8_t const* state, uint16_t)
    {
        printf("SM write 0, state %x \n", *state);
        return DatagramState::OK;
    };

    auto process_read = [&mailbox](DatagramHeader const*, uint8_t const* state, uint16_t)
    {
        printf("SM read 1, state %x \n", *state);
        if ((*state & MAILBOX_STATUS) == MAILBOX_STATUS)
        {
            printf("Mailbox can read ! \n");
            mailbox.can_read = true;
        }
        else
        {
            mailbox.can_read = false;
        }
        return DatagramState::OK;
    };


    auto process_read_data = [](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
    {
        if (wkc != 1)
        {
            printf("WKC invalid\n");
            return DatagramState::INVALID_WKC;
        }

        uint32_t result = *reinterpret_cast<uint32_t const*>(data);
        printf("Result %x \n", result);

        return DatagramState::OK;
    };



    // Go to preop
    sleep(1s);
    uint16_t data = State::PRE_OP;
    Frame frame;
    frame.addDatagram(0, Command::BWR, createAddress(0, reg::AL_CONTROL), &data, sizeof(data));
    link->writeThenRead(frame);

    sleep(1s);
    // while(true)
    // {
        link->addDatagram(Command::FPRD, createAddress(slave_address, reg::SYNC_MANAGER_0 + reg::SM_STATS), nullptr, 1, process_write, error);

        link->addDatagram(Command::FPRD, createAddress(slave_address, reg::SYNC_MANAGER_1 + reg::SM_STATS), nullptr, 1, process_read, error);

        link->processDatagrams();

        if (mailbox.can_read)
        {
            printf("retrieve waiting message \n");
            int32_t i = 0;
            while (i < 10)
            {
                uint8_t bonjour[128];
                link->addDatagram(Command::FPRD, createAddress(slave_address, mailbox.send_offset), bonjour, mailbox.send_size, process_read_data, error);
                // link->addDatagram(Command::FPRD, createAddress(slave_address, mailbox.recv_offset), nullptr, mailbox.send_size, process_read_data, error);
                link->processDatagrams();
                i++;
                sleep(100ms);
            }
        }
    //     sleep(100ms);
    // }
    return 0;
}
