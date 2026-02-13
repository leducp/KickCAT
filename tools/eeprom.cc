// Tool to dump and flash the eeprom of a slave on the bus.
// Current state: POC, can only dump eeprom file.

#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"
#include "kickcat/helpers.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#elif __MINGW64__
    #include "kickcat/OS/Windows/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <fstream>
#include <iostream>
#include <argparse/argparse.hpp>

using namespace kickcat;

bool askContinue()
{
    while (true)
    {
        printf("Do you want to continue? (y/n): ");

        char input[16];
        char answer;
        if (fgets(input, sizeof(input), stdin) != nullptr)
        {
            // Parse the first non-whitespace character
            if (sscanf(input, " %c", &answer) == 1)
            {
                if (answer == 'y' || answer == 'Y')
                {
                    return true;
                }
                else if (answer == 'n' || answer == 'N')
                {
                    return false;
                }
            }
        }
    }
}

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("eeprom");

    int slave_index;
    program.add_argument("-s", "--slave")
        .help("slave number (starts at 0)")
        .required()
        .scan<'i', int>()
        .store_into(slave_index);

    std::string order_raw;
    program.add_argument("-c", "--command")
        .help("command: read or write")
        .required()
        .store_into(order_raw);

    std::string file;
    program.add_argument("-f", "--file")
        .help("file to read from or write to")
        .required()
        .store_into(file);

    std::string nom_interface_name;
    program.add_argument("-i", "--interface")
        .help("network interface name")
        .required()
        .store_into(nom_interface_name);

    std::string red_interface_name;
    program.add_argument("-r", "--redundancy")
        .help("redundancy network interface name")
        .default_value(std::string{"null"})
        .store_into(red_interface_name);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    std::shared_ptr<AbstractSocket> socket_redundancy;

    if (red_interface_name == "null")
    {
        printf("No redundancy mode selected \n");
        socket_redundancy = std::make_shared<SocketNull>();
    }
    else
    {
        socket_redundancy = std::make_shared<Socket>();
    }

    selectInterface(nom_interface_name, red_interface_name);

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

    enum Order
    {
        READ,
        WRITE
    };

    Order order;
    if (order_raw == "write")
    {
        order = Order::WRITE;
    }
    else if (order_raw == "read")
    {
        order = Order::READ;
    }
    else
    {
        printf("Invalid command %s \n", order_raw.c_str());
        return 1;
    }


    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(10ms);
    link->checkRedundancyNeeded();

    Bus bus(link);
    bus.configureWaitLatency(1ms, 10ms);

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
    catch (ErrorAL const& e)
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

    if (order == Order::READ)
    {
        char const* raw_data = reinterpret_cast<char const*>(slave.sii.eeprom.data());
        std::ofstream f(file, std::ofstream::binary);

        // Write dumped data
        f.write(raw_data, slave.sii.eeprom.size() * sizeof(uint32_t));
        f.close();
        printf("Saving eeprom to %s: done\n", file.c_str());
    }

    if (order == Order::WRITE)
    {
        bool addressingScheme2Bytes = false;
        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error while fetching eeprom addressing scheme", state);
        };

        auto process = [&addressingScheme2Bytes](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }
            uint16_t answer;
            std::memcpy(&answer, data, sizeof(uint16_t));
            if (answer & eeprom::Control::ALGO_SEL)
            {
                addressingScheme2Bytes = true;
            }
            return DatagramState::OK;
        };

        link->addDatagram(Command::FPRD, createAddress(slave.address, reg::EEPROM_CONTROL), nullptr, 2, process, error);
        link->processDatagrams();

        // Load eeprom data
        std::vector<uint16_t> buffer;
        std::ifstream eeprom_file;
        eeprom_file.open(file, std::ios::binary | std::ios::ate);
        if (not eeprom_file.is_open())
        {
            THROW_ERROR("Cannot load EEPROM");
        }
        std::size_t size = eeprom_file.tellg();
        eeprom_file.seekg (0, std::ios::beg);
        buffer.resize(size / 2); // vector of uint16_t so / 2 since the size is in byte
        eeprom_file.read((char*)buffer.data(), size);
        eeprom_file.close();

        if (addressingScheme2Bytes == false)
        {
            // max eeprom size is 16Kb
            if (size > 2_KiB)
            {
                printf("!!! WARNING !!!\n");
                printf("Current EEPROM addressing scheme is one byte: max EEPROM size is 16Kbit (2KiB)\n");
                printf("but the given EEPROM file size is %ldB which exceed the maximum possible target size\n", size);
                printf("If you wish to continue, the written size will be truncated to 2KiB\n");

                if (not askContinue())
                {
                    return 0;
                }
                buffer.resize(2_KiB / 2);
            }
        }

        for (uint32_t i = 0; i < buffer.size(); i++)
        {
            bus.writeEeprom(slave, i, static_cast<void*>(&buffer[i]), 2);
            printf("\r Updating: %d/%lu", i + 1, buffer.size());
            fflush(stdout);
        }
        printf("\n");

        printf("Wait for Err led to go off on the board.\nReset device to trigger reloading of new EEPROM.\n");
    }
    return 0;
}
