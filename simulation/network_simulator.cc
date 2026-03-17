#include <algorithm>
#include <argparse/argparse.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numeric>

#include "kickcat/CoE/EsiParser.h"
#include "kickcat/CoE/mailbox/response.h"
#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/Frame.h"
#include "kickcat/OS/Time.h"
#include "kickcat/PDO.h"
#include "kickcat/helpers.h"
#include "kickcat/slave/Slave.h"

using namespace kickcat;
using namespace kickcat::slave;
using json = nlohmann::json;
namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("network_simulator");

    std::string interface;
    program.add_argument("-i", "--interface")
        .help("network interface name")
        .required()
        .store_into(interface);

    std::vector<std::string> slave_configs;
    program.add_argument("-s", "--slaves")
        .help("JSON configuration files for slaves")
        .remaining()
        .store_into(slave_configs);

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

    if (slave_configs.empty())
    {
        std::cerr << "No slave configuration files provided" << std::endl;
        std::cerr << program;
        return 1;
    }

    size_t slave_count = slave_configs.size();
    std::vector<std::unique_ptr<EmulatedESC>> escs;
    std::vector<std::unique_ptr<PDO>> pdos;
    std::vector<std::unique_ptr<Slave>> slaves;
    std::vector<std::unique_ptr<mailbox::response::Mailbox>> mailboxes;
    std::vector<std::vector<uint8_t>> input_pdo;
    std::vector<std::vector<uint8_t>> output_pdo;

    escs.reserve(slave_count);
    pdos.reserve(slave_count);
    slaves.reserve(slave_count);
    mailboxes.reserve(slave_count);
    input_pdo.reserve(slave_count);
    output_pdo.reserve(slave_count);

    constexpr uint32_t PDO_MAX_SIZE = 32;
    CoE::EsiParser parser;

    for (const auto& config_path : slave_configs)
    {
        fs::path p(config_path);
        fs::path config_dir = p.parent_path();

        std::ifstream f(config_path);
        if (not f.is_open())
        {
            std::cerr << "Failed to open config file: " << config_path << std::endl;
            return 1;
        }

        json config;
        try
        {
            f >> config;
        }
        catch (const json::parse_error& e)
        {
            std::cerr << "Failed to parse JSON in " << config_path << ": " << e.what() << std::endl;
            return 1;
        }

        if (not config.contains("eeprom"))
        {
            std::cerr << "Config file " << config_path << " missing 'eeprom' field" << std::endl;
            return 1;
        }

        std::string eeprom_path = config["eeprom"];
        fs::path eeprom_full_path = config_dir / eeprom_path;
        auto esc = std::make_unique<EmulatedESC>(eeprom_full_path.string().c_str());
        auto pdo = std::make_unique<PDO>(esc.get());
        auto slave = std::make_unique<Slave>(esc.get(), pdo.get());

        if (config.contains("coe_xml"))
        {
            std::string coe_xml_path = config["coe_xml"];
            fs::path coe_xml_full_path = config_dir / coe_xml_path;
            auto mbx = std::make_unique<mailbox::response::Mailbox>(esc.get(), 1024);
            auto dictionary = parser.loadFile(coe_xml_full_path.string());
            mbx->enableCoE(std::move(dictionary));
            slave->setMailbox(mbx.get());
            mailboxes.push_back(std::move(mbx));
        }

        input_pdo.emplace_back(PDO_MAX_SIZE);
        std::iota(input_pdo.back().begin(), input_pdo.back().end(), 0);
        output_pdo.emplace_back(PDO_MAX_SIZE, 0xFF);

        pdo->setInput(input_pdo.back().data(), PDO_MAX_SIZE);
        pdo->setOutput(output_pdo.back().data(), PDO_MAX_SIZE);

        escs.push_back(std::move(esc));
        pdos.push_back(std::move(pdo));
        slaves.push_back(std::move(slave));
    }

    // Configure DL status for each ESC based on its position in the chain.
    // Port 0 is always connected (upstream to master or previous slave).
    // Port 1 is connected if there is a downstream slave.
    for (size_t i = 0; i < escs.size(); ++i)
    {
        uint16_t dl_status = 0;
        dl_status |= (1 << 4);  // PL_port0
        dl_status |= (1 << 9);  // COM_port0

        if (i + 1 < escs.size())
        {
            dl_status |= (1 << 5);  // PL_port1
            dl_status |= (1 << 11); // COM_port1
        }

        escs[i]->write(reg::ESC_DL_STATUS, &dl_status, sizeof(dl_status));
    }

    printf("Start EtherCAT network simulator on %s with %zu slaves\n", interface.c_str(), escs.size());
    auto [socket, _] = createSockets(interface, "");
    socket->setTimeout(-1ns);

    std::vector<nanoseconds> stats;
    stats.reserve(1000);

    for (auto& slave : slaves)
    {
        slave->start();
    }

    uint32_t iteration_counter = 0;
    uint8_t current_value = 0x11;
    constexpr uint32_t ITER = 1000;

    while (true)
    {
        Frame frame;
        int32_t r = socket->read(frame.data(), ETH_MAX_SIZE);
        if (r < 0)
        {
            printf("Something wrong happened. Aborting...\n");
            return -1;
        }

        auto t1 = since_epoch();

        while (true)
        {
            auto [header, data, wkc] = frame.peekDatagram();
            if (header == nullptr)
            {
                break;
            }

            for (auto& esc : escs)
            {
                esc->processDatagram(header, data, wkc);
            }
        }

        for (size_t i = 0; i < slaves.size(); ++i)
        {
            slaves[i]->routine();
            if (slaves[i]->state() == State::SAFE_OP)
            {
                if (output_pdo[i][1] != 0xFF)
                {
                    slaves[i]->validateOutputData();
                }
            }

            std::fill(input_pdo[i].begin(), input_pdo[i].end(), current_value);
        }

        // Update input buffer every ITER iterations
        iteration_counter++;
        if (iteration_counter >= ITER)
        {
            iteration_counter = 0;
			// Move to next value: 0x11 -> 0x22 -> 0x33 -> ... -> 0xFF -> 0x00 -> 0x11
            if (current_value == 0xFF)
            {
                current_value = 0x00;
            }
            else
            {
                current_value += 0x11;
            }
        }

        int32_t written = socket->write(frame.data(), r);
        if (written < 0)
        {
            printf("Write back frame: something wrong happened. Aborting...\n");
            return -2;
        }
        auto t2 = since_epoch();

        stats.push_back(t2 - t1);
        if (stats.size() >= 1000)
        {
            std::sort(stats.begin(), stats.end());

            printf("[%f] frame processing time: \n\t min: %f\n\t max: %f\n\t avg: %f\n", seconds_f(since_start()).count(),
                   stats.front().count() / 1000.0,
                   stats.back().count() / 1000.0,
                   (std::reduce(stats.begin(), stats.end()) / stats.size()).count() / 1000.0);
            stats.clear();
        }
    }

    return 0;
}
