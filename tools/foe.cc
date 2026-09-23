#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>

#include "kickcat/Bus.h"
#include "kickcat/FoE/protocol.h"
#include "kickcat/Link.h"
#include "kickcat/OS/Filesystem.h"
#include "kickcat/helpers.h"

using namespace kickcat;

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("foe");
    program.add_description("Read or write a file on a slave with FoE (File over EtherCAT).");

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

    int slave_index = 0;
    program.add_argument("-s", "--slave")
        .help("slave position on the bus (starts at 0)")
        .required()
        .scan<'i', int>()
        .store_into(slave_index);

    std::string command;
    program.add_argument("-c", "--command")
        .help("read (slave to file) or write (file to slave)")
        .required()
        .choices("read", "write")
        .store_into(command);

    std::string file;
    program.add_argument("-f", "--file")
        .help("local file")
        .required()
        .store_into(file);

    std::string name;
    program.add_argument("-n", "--name")
        .help("file name on the slave (default: the local file name)")
        .default_value(std::string{""})
        .store_into(name);

    uint32_t password = 0;
    program.add_argument("-p", "--password")
        .help("FoE password, decimal or 0x prefixed hexadecimal (default: 0, no password)")
        .default_value(uint32_t{0})
        .scan<'i', uint32_t>()
        .store_into(password);

    int timeout_ms = 5000;
    program.add_argument("-t", "--timeout")
        .help("maximum time of each exchange with the slave, in milliseconds")
        .default_value(5000)
        .scan<'i', int>()
        .store_into(timeout_ms);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (std::runtime_error const& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    if (name.empty())
    {
        name = filesystem::filename(file);
    }

    std::vector<uint8_t> content;
    if (command == "write")
    {
        try
        {
            content = filesystem::readFile(file);
        }
        catch (std::exception const& e)
        {
            std::cerr << "Cannot read " << file << ": " << e.what() << std::endl;
            return 1;
        }
        if (content.size() > std::numeric_limits<uint32_t>::max())
        {
            std::cerr << file << " is too big for FoE" << std::endl;
            return 1;
        }
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
    link->checkRedundancyNeeded();

    Bus bus(link);

    try
    {
        bus.init();
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

    if ((slave_index < 0) or (static_cast<std::size_t>(slave_index) >= bus.slaves().size()))
    {
        std::cerr << "No slave at position " << slave_index << ": " << bus.slaves().size() << " slave(s) on the bus" << std::endl;
        return 1;
    }
    Slave& slave = bus.slaves().at(static_cast<std::size_t>(slave_index));

    if (not (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::FoE))
    {
        std::cerr << "Warning: the slave SII does not advertise FoE, trying anyway" << std::endl;
    }

    uint32_t total = static_cast<uint32_t>(content.size());
    auto progress = [&command, total](uint32_t transferred)
    {
        if (command == "write")
        {
            printf("\r Writing: %u/%u bytes", transferred, total);
        }
        else
        {
            printf("\r Reading: %u bytes", transferred);
        }
        fflush(stdout);
    };

    nanoseconds timeout = timeout_ms * 1ms;
    try
    {
        if (command == "read")
        {
            bus.readFoE(slave, name, password, content, timeout, progress);
        }
        else
        {
            bus.writeFoE(slave, name, password, std::move(content), timeout, progress);
        }
    }
    catch (ErrorFoE const& e)
    {
        printf("\n");
        std::cerr << e.what() << ": " << FoE::errorToString(static_cast<uint32_t>(e.code())) << std::endl;
        return 1;
    }
    catch (std::exception const& e)
    {
        printf("\n");
        std::cerr << e.what() << std::endl;
        return 1;
    }
    printf("\n");

    if (command == "read")
    {
        try
        {
            filesystem::writeFile(file, content.data(), content.size());
        }
        catch (std::exception const& e)
        {
            std::cerr << "Cannot write " << file << ": " << e.what() << std::endl;
            return 1;
        }
        printf("Read '%s' (%zu bytes) into %s\n", name.c_str(), content.size(), file.c_str());
    }
    else
    {
        printf("Wrote %s (%u bytes) as '%s'\n", file.c_str(), total, name.c_str());
    }

    return 0;
}
