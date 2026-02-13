#include <iostream>
#include <cstring>
#include <cmath>
#include <argparse/argparse.hpp>

#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/Mailbox.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"


using namespace kickcat;


void printObjectDictionnaryList(Bus& bus, Slave& slave, CoE::SDO::information::ListType type)
{
    uint16_t buffer[2048];
    uint32_t buffer_size = 4096; // in bytes

    auto sdo = slave.mailbox.createSDOInfoGetODList(type, &buffer, &buffer_size, 100ms);

    bus.waitForMessage(sdo);
    if (sdo->status() != mailbox::request::MessageStatus::SUCCESS)
    {
        THROW_ERROR_CODE("Error while get Object Dictionnary List", error::category::CoE, sdo->status());
    }

    printf("Data size received %u \n", buffer_size);

    uint16_t index_size = buffer_size / 2 - 1;
    printf("Object dictionnary list: size: %u\n", index_size);

    for (int i = 0; i < index_size; ++i)
    {
        printf("index %04x \n", buffer[i + 1]);
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
        THROW_ERROR_CODE("Error while get Object Description", error::category::CoE, sdo->status());
    }

    CoE::SDO::information::ObjectDescription* description = reinterpret_cast<CoE::SDO::information::ObjectDescription*>(buffer);
    std::string name{buffer + sizeof(CoE::SDO::information::ObjectDescription), buffer_size - sizeof(CoE::SDO::information::ObjectDescription)};

    printf("Received object %s\n desc: %s\n", name.c_str(), toString(*description).c_str());
}


void printEntryDescription(Bus& bus, Slave& slave, uint16_t index, uint8_t subindex, uint8_t value_info = CoE::SDO::information::ValueInfo::DEFAULT)
{
    char buffer[4096];
    uint32_t buffer_size = 4096; // in bytes

    auto sdo = slave.mailbox.createSDOInfoGetED(index, subindex, value_info, &buffer, &buffer_size, 100ms);
    bus.waitForMessage(sdo);
    if (sdo->status() != mailbox::request::MessageStatus::SUCCESS)
    {
        THROW_ERROR_CODE("Error while get Entry Description", error::category::CoE, sdo->status());
    }

    CoE::SDO::information::EntryDescription* description = reinterpret_cast<CoE::SDO::information::EntryDescription*>(buffer);
    std::string name{buffer + sizeof(CoE::SDO::information::EntryDescription), buffer_size - sizeof(CoE::SDO::information::EntryDescription)};
    printf("Received entry %s\n desc: %s\n", name.c_str(), toString(*description).c_str());
}


int main(int argc, char *argv[])
{
    argparse::ArgumentParser program("ingenia_info");

    std::string nom_interface_name;
    program.add_argument("-i", "--interface")
        .help("network interface name")
        .required()
        .store_into(nom_interface_name);

    std::string red_interface_name;
    program.add_argument("-r", "--redundancy")
        .help("redundancy network interface name")
        .default_value(std::string{""})
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

    std::shared_ptr<AbstractSocket> socket_nominal;
    std::shared_ptr<AbstractSocket> socket_redundancy;
    try
    {
        auto [nominal, redundancy] = createSockets(nom_interface_name, red_interface_name);
        socket_nominal = nominal;
        socket_redundancy = redundancy;
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
    catch (ErrorAL const &e)
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
    //printObjectDictionnaryList(bus, ingenia, CoE::SDO::information::ListType::NUMBER);
    printObjectDictionnaryList(bus, ingenia, CoE::SDO::information::ListType::ALL);


    printObjectDescription(bus, ingenia, 0x1018);
    printEntryDescription(bus, ingenia, 0x1018, 0,
        CoE::SDO::information::ValueInfo::MAXIMUM);

    printEntryDescription(bus, ingenia, 0x1600, 0,
        CoE::SDO::information::ValueInfo::DEFAULT);

    printEntryDescription(bus, ingenia, 0x1600, 0,
        CoE::SDO::information::ValueInfo::DEFAULT | CoE::SDO::information::ValueInfo::MINIMUM | CoE::SDO::information::ValueInfo::MAXIMUM);


    return 0;
}
