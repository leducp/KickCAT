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
        printf("Eeprom status %x \n", answer);
        if (answer & 0xA000) // EEPROM busy or Missing EEPROM acknowledge or invalid command
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

bool isEepromAcknowleded(Link& link, Slave& slave)
{
    bool acknowleded = true;
    auto process = [&acknowleded](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
    {
        if (wkc != 1)
        {
            return DatagramState::INVALID_WKC;
        }
        uint16_t answer = *reinterpret_cast<uint16_t const*>(data);
        printf("Eeprom status %x \n", answer);
        if (answer & 0x2000) // EEPROM busy or Missing EEPROM acknowledge or invalid command
        {
            acknowleded = false;
        }
        return DatagramState::OK;
    };

    auto error = [](DatagramState const&)
    {
        THROW_ERROR("Error while fetching eeprom state");
    };

    link.addDatagram(Command::FPRD, createAddress(slave.address, reg::EEPROM_CONTROL), nullptr, 2, process, error);
    link.processDatagrams();

    return acknowleded;
}


// TODO check size <= 2 ?
// TODO speed up write by grouping datagrams in frame ?
void writeEeprom(Link& link, Slave& slave, uint16_t address, void* data, uint16_t size)
{
    eeprom::Request req;

    // Read result
    auto error = [](DatagramState const& state)
    {
        printf("State %s \n", toString(state));
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
    // wait for eeprom to be ready
    if (not isEepromReady(link, slave))
    {
        THROW_ERROR("Timeout");
    }

    printf("Chunk %x, address %x \n", *static_cast<uint16_t*>(data), address);
    link.addDatagram(Command::FPWR, createAddress(slave.address, reg::EEPROM_DATA), data, size, process, error);
    link.processDatagrams();

    // Request specific address
    req = {eeprom::Command::WRITE, address, 0};

    bool acknowledged = false;

    nanoseconds start_time = since_epoch();
    while (not acknowledged)
    {
        link.addDatagram(Command::FPWR, createAddress(slave.address, reg::EEPROM_CONTROL), &req, sizeof(req), process, error);
        link.processDatagrams();
        acknowledged = isEepromAcknowleded(link, slave);
        sleep(1ms);

        if (elapsed_time(start_time) > 10ms)
        {
            THROW_ERROR("Timeout acknowledge write eeprom");
        }
    }

    // wait for eeprom to be ready
    if (not isEepromReady(link, slave))
    {
        THROW_ERROR("Timeout");
    }
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

//    uint16_t test = 0x6070;
//    writeEeprom(*link, bus.slaves()[0], 0x44, static_cast<void*>(&test), 2);
//    writeEeprom(*link, bus.slaves()[0], 0x45, static_cast<void*>(&test), 2);
//    writeEeprom(*link, bus.slaves()[0], 0x46, static_cast<void*>(&test), 2);
//    writeEeprom(*link, bus.slaves()[0], 0x47, static_cast<void*>(&test), 2);

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
    for (auto& chunk : buffer)
    {
        writeEeprom(*link, bus.slaves()[0], pos, static_cast<void*>(&chunk), 2);
        pos++;
    }

    // Read result
    auto error = [](DatagramState const& state)
    {
        printf("State %s \n", toString(state));
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

//    uint16_t cmd = eeprom::Command::RELOAD;
//    // Request specific address
//    link->addDatagram(Command::FPWR, createAddress(bus.slaves()[0].address, reg::EEPROM_CONTROL), &cmd, sizeof(eeprom::Command), process, error);
//    link->processDatagrams();

    return 0;
}
