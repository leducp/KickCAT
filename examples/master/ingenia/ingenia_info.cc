#include <iostream>
#include <cstring>
#include <cmath>

#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/Mailbox.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"
#include "kickcat/helpers.h"

#include "CanOpenErrors.h"
#include "CanOpenStateMachine.h"
#include "IngeniaProtocol.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#elif __MINGW64__
    #include "kickcat/OS/Windows/Socket.h"
#else
#error "Unknown platform"
#endif


using namespace kickcat;


void printObjectDictionnaryList(Bus& bus, Slave& slave, CoE::SDO::information::ListType type)
{
    uint16_t buffer[2048];
    uint32_t buffer_size = 4096; // in bytes

    auto sdo = slave.mailbox.createSDOInfoGetODList(type, &buffer, &buffer_size, 100ms);

    bus.waitForMessage(sdo);
    if (sdo->status() != mailbox::request::MessageStatus::SUCCESS)
    {
        THROW_ERROR_CODE("Error while get Object Dictionnary List", sdo->status());
    }

    printf("Data size received %u \n", buffer_size);
    std::vector<uint16_t> index_list{buffer + sizeof(type), buffer + buffer_size/2};
    printf("Object dictionnary list: size: %li\n", index_list.size());

    for (auto const& index : index_list)
    {
        printf("index %x \n", index);
    }
}


void printObjectDescription(Bus& bus, Slave& slave, uint16_t index)
{
    char buffer[4096];
    uint32_t buffer_size = 4096; // in bytes

    auto sdo = slave.mailbox.createSDOInfoGetOD(index, &buffer, &buffer_size, 100ms);
    bus.waitForMessage(sdo);
    if (sdo->status() != mailbox::request::MessageStatus::SUCCESS)
    {
        THROW_ERROR_CODE("Error while get Object Description", sdo->status());
    }

    CoE::SDO::information::ObjectDescription* description = reinterpret_cast<CoE::SDO::information::ObjectDescription*>(buffer);
    std::string name{buffer + sizeof(CoE::SDO::information::ObjectDescription), buffer_size - sizeof(CoE::SDO::information::ObjectDescription)};

    printf("Received object %s desc: %s ", name.c_str(), toString(*description).c_str());
}


void printEntryDescription(Bus& bus, Slave& slave, uint16_t index, uint8_t subindex, uint8_t value_info)
{
    char buffer[4096];
    uint32_t buffer_size = 4096; // in bytes

    auto sdo = slave.mailbox.createSDOInfoGetED(index, subindex, value_info, &buffer, &buffer_size, 100ms);
    bus.waitForMessage(sdo);
    if (sdo->status() != mailbox::request::MessageStatus::SUCCESS)
    {
        THROW_ERROR_CODE("Error while get Entry Description", sdo->status());
    }

    CoE::SDO::information::EntryDescription* description = reinterpret_cast<CoE::SDO::information::EntryDescription*>(buffer);
    printf("Received entry desc: %s \n", toString(*description).c_str());

    for (uint32_t i = sizeof(CoE::SDO::information::EntryDescription); i < buffer_size; ++i)
    {
        printf("%c", buffer[i]);
    }
    printf("\n");
}


int main(int argc, char *argv[])
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

    selectInterface(nom_interface_name, red_interface_name);

    auto socket_nominal = std::make_shared<Socket>();
    try
    {
        socket_nominal->open(nom_interface_name);
        socket_redundancy->open(red_interface_name);
    }
    catch (std::exception const &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto report_redundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(2ms);
    link->checkRedundancyNeeded();

    Bus bus(link);
    try
    {
        bus.init();

        for (auto& slave: bus.slaves())
        {
            printInfo(slave);
            printESC(slave);
        }
    }
    catch (ErrorCode const &e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto& ingenia = bus.slaves().at(0);
    //printObjectDictionnaryList(bus, ingenia, CoE::SDO::information::ListType::ALL);
    printObjectDescription(bus, ingenia, 0x2025);
    printEntryDescription(bus, ingenia, 0x2025, 0,
        CoE::SDO::information::ValueInfo::MAXIMUM);

    printEntryDescription(bus, ingenia, 0x2025, 0,
        CoE::SDO::information::ValueInfo::DEFAULT);

    printEntryDescription(bus, ingenia, 0x2373, 0,
        CoE::SDO::information::ValueInfo::DEFAULT | CoE::SDO::information::ValueInfo::MINIMUM | CoE::SDO::information::ValueInfo::MAXIMUM);

    return 0;
}
