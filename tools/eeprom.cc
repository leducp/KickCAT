// Tool to dump and flash the eeprom of a slave on the bus.
// Current state: POC, can only dump eeprom file.

#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"

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

int main(int argc, char* argv[])
{
    if ((argc != 5) and (argc != 6))
    {
        printf("argc: %d\n", argc);
        printf("usage redundancy mode :    ./eeprom [slave_number] [command] [file] NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./eeprom [slave_number] [command] [file] NIC_nominal\n");
        printf("Available commands are: \n");
        printf("\t -dump: read the eeprom of the slave and write it in [file].\n");
        printf("\t -update: copy the given [file] into the eeprom of the slave.\n");
        return 1;
    }

    std::shared_ptr<AbstractSocket> socket_redundancy;
    int slave_index     = std::stoi(argv[1]);
    std::string command = argv[2];
    std::string file    = argv[3];
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[4];
    bool shall_update = false;
    bool shall_dump   = false;

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

    if (command == "update")
    {
        shall_update = true;
    }
    else if (command == "dump")
    {
        shall_dump = true;
    }
    else
    {
        printf("Invalid command %s \n", command.c_str());
        return 1;
    }


    std::shared_ptr<Link> link= std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(10ms);
    link->checkRedundancyNeeded();

    Bus bus(link);

    // To interact with the EEPROM we don't need to go through the EtherCAT state machine, use a minimal init to avoid
    // being stuck on non configured slaves.
    try
    {
        if (bus.detectSlaves() == 0)
        {
            THROW_ERROR("No slave detected");
        }
        uint16_t param = 0x0;
        bus.broadcastWrite(reg::EEPROM_CONFIG, &param, 2);
        bus.setAddresses();
        bus.fetchEeprom();
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

    Slave& slave = bus.slaves().at(slave_index);

    if (shall_dump)
    {
        auto const& sii = slave.sii.buffer;
        char const* raw_data = reinterpret_cast<char const*>(sii.data());
        std::ofstream f(file, std::ofstream::binary);

        // Create an eeprom binary file with the right size of empty data
        std::vector<char> empty(slave.eeprom_size, -1);
        f.write(empty.data(), empty.size());
        f.seekp(0);

        // Write dumped data
        f.write(raw_data, sii.size() * 4);
        f.close();
    }

    if (shall_update)
    {
        // Load eeprom data
        std::vector<uint16_t> buffer;
        std::ifstream eeprom_file;
        eeprom_file.open(file, std::ios::binary | std::ios::ate);
        if (not eeprom_file.is_open())
        {
            THROW_ERROR("Cannot load EEPROM");
        }
        int size = eeprom_file.tellg();
        eeprom_file.seekg (0, std::ios::beg);
        buffer.resize(size / 2); // vector of uint16_t so / 2 since the size is in byte
        eeprom_file.read((char*)buffer.data(), size);
        eeprom_file.close();

        uint32_t pos = 0;
        for (uint32_t i = 0; i < buffer.size(); i++)
        {
            bus.writeEeprom(slave, pos, static_cast<void*>(&buffer[i]), 2);
            printf("\r Updating: %d/%lu", i+1, buffer.size());
            fflush(stdout);
        }
        printf("\n");

        // Read result
        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error while writing eeprom data", state);
        };

        auto process = [](DatagramHeader const*, void const*, uint16_t wkc)
        {
            if (wkc != 1)
            {
                printf("Process INVALID WKC \n");
                return DatagramState::INVALID_WKC;
            }
            return DatagramState::OK;
        };

        uint16_t cmd = eeprom::Command::RELOAD;
        link->addDatagram(Command::FPWR, createAddress(bus.slaves()[0].address, reg::EEPROM_CONTROL), &cmd, sizeof(eeprom::Command), process, error);
        link->processDatagrams();
    }

    return 0;
}
