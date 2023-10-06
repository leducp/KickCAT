// Tool to dump and flash the eeprom of a slave on the bus.
// Current state: POC, can only dump eeprom file.

#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"
#include "kickcat/Time.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <fstream>
#include <iostream>

using namespace kickcat;

bool isEepromReady(Link& link, Slave& slave)
{
    bool ready = true;
    auto process = [&ready](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
    {
        if (wkc != 1)
        {
            return DatagramState::INVALID_WKC;
        }
        uint16_t answer = *reinterpret_cast<uint16_t const*>(data);
        if (answer & 0x8000)
        {
            ready = false;
        }
        return DatagramState::OK;
    };

    auto error = [](DatagramState const&)
    {
        THROW_ERROR("Error while fetching eeprom state");
    };

    for (int i = 0; i < 10; ++i)
    {
        sleep(200us);
        link.addDatagram(Command::FPRD, createAddress(slave.address, reg::EEPROM_CONTROL), nullptr, 2, process, error);

        ready = true; // rearm check
        try
        {
            link.processDatagrams();
        }
        catch (...)
        {
            return false;
        }

        if (ready)
        {
            return ready;
        }
    }

    return false;
}


void writeEeprom(Link& link, Slave& slave, uint16_t address, void* data, uint16_t size)
{
    eeprom::Request req;

    // Read result
    auto error = [](DatagramState const& state)
    {
        THROW_ERROR_DATAGRAM("Error while fetching eeprom data", state);
    };

    auto process = [](DatagramHeader const*, void const*, uint16_t wkc)
    {
        if (wkc != 1)
        {
            return DatagramState::INVALID_WKC;
        }
        return DatagramState::OK;
    };

    link.addDatagram(Command::FPWR, createAddress(slave.address, reg::EEPROM_DATA), data, size, process, error);
    link.processDatagrams();

    // wait for eeprom to be ready
    if (not isEepromReady(link, slave))
    {
        THROW_ERROR("Timeout");
    }

    // Request specific address
    req = {eeprom::Command::WRITE, address, 0};

    link.addDatagram(Command::FPWR, createAddress(slave.address, reg::EEPROM_CONTROL), &req, sizeof(req), process, error);
    link.processDatagrams();
}


int main(int argc, char* argv[])
{
    if ((argc != 5) and (argc != 6))
    {
        printf("argc: %d\n", argc);
        printf("usage redundancy mode :    ./eeprom [slave_number] [command] [file] NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./eeprom [slave_number] [command] [file] NIC_nominal\n");
        return 1;
    }


    std::shared_ptr<AbstractSocket> socket_redundancy;
    int slave_index     = std::stoi(argv[1]);
    std::string command = argv[2];
    std::string file    = argv[3];
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[4];

    if (argc == 5)
    {
        printf("No redundancy mode selected \n");
        socket_redundancy = std::make_shared<SocketNull>();
    }
    else
    {
        socket_redundancy = std::make_shared<Socket>();
        red_interface_name = argv[5];
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
    link->setTimeout(10ms);

    Bus bus(link);
    try
    {
        bus.init();
    }
    catch (ErrorCode const& e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    uint32_t data = 0x1234;
    writeEeprom(*link, bus.slaves()[0], 2, static_cast<void*>(&data), 4);



    return 0;
}
