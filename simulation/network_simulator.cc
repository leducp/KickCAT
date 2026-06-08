#include <algorithm>
#include <argparse/argparse.hpp>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numeric>
#include <optional>

#include "kickcat/CoE/OD.h"
#include "kickcat/ESI/Parser.h"
#include "kickcat/ESI/SIIBuilder.h"
#include "kickcat/CoE/mailbox/response.h"
#include "kickcat/EmulatedNetwork.h"
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

static volatile std::sig_atomic_t running = 1;
static void signalHandler(int) { running = 0; }

// Example fault injection: SIGUSR1 breaks a configured wire, SIGUSR2 heals it.
// The handler only flags the request; it is applied from the main loop where the
// network object is in scope (setLinkState is not async-signal-safe). SIGUSR1/2 are
// POSIX-only; on platforms without them the break/heal API is still usable directly.
static volatile std::sig_atomic_t link_event = 0;  // 0: none, 1: break, 2: heal
#ifdef SIGUSR1
static void linkSignalHandler(int sig)
{
    if (sig == SIGUSR1) { link_event = 1; }
    if (sig == SIGUSR2) { link_event = 2; }
}
#endif

int main(int argc, char* argv[])
{
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifdef SIGUSR1
    std::signal(SIGUSR1, linkSignalHandler);
    std::signal(SIGUSR2, linkSignalHandler);
#endif

    argparse::ArgumentParser program("network_simulator");

    std::string interface;
    program.add_argument("-i", "--interface")
        .help("network interface name")
        .required()
        .store_into(interface);

    int slave_number = 0;
    program.add_argument("-n", "--count")
        .help("Number of slaves to simulate")
        .default_value(0)
        .scan<'i', int>()
        .store_into(slave_number);

    std::string redundancy_interface;
    program.add_argument("-r", "--redundancy")
        .help("redundancy network interface (enables cable-redundancy routing)")
        .default_value(std::string{})
        .store_into(redundancy_interface);

    std::vector<int> break_link;
    program.add_argument("--break-link")
        .help("two slave indices A B; SIGUSR1 breaks / SIGUSR2 heals the wire between them")
        .nargs(2)
        .scan<'i', int>()
        .store_into(break_link);

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

    std::vector<std::string> expanded_slave_configs;

    if (slave_number > 0)
    {
        if (slave_configs.size() != 1)
        {
            std::cerr << "When using --count/-n, you must provide exactly one JSON config file with --slaves/-s" << std::endl;
            return 1;
        }

        expanded_slave_configs.reserve(slave_number);
        for (int i = 0; i < slave_number; ++i)
        {
            expanded_slave_configs.push_back(slave_configs[0]);
        }
    }
    else
    {
        expanded_slave_configs = slave_configs;
    }

    if (expanded_slave_configs.empty())
    {
        std::cerr << "No slave configuration files provided" << std::endl;
        std::cerr << program;
        return 1;
    }

    size_t slave_count = expanded_slave_configs.size();
    std::vector<std::unique_ptr<EmulatedESC>> escs;
    std::vector<std::unique_ptr<PDO>> pdos;
    std::vector<std::unique_ptr<Slave>> slaves;
    std::vector<std::unique_ptr<mailbox::response::Mailbox>> mailboxes;
    std::vector<std::unique_ptr<CoE::Dictionary>> dictionaries;  // application owns the ODs
    std::vector<std::vector<uint8_t>> input_pdo;
    std::vector<std::vector<uint8_t>> output_pdo;

    escs.reserve(slave_count);
    pdos.reserve(slave_count);
    slaves.reserve(slave_count);
    mailboxes.reserve(slave_count);
    dictionaries.reserve(slave_count);
    input_pdo.reserve(slave_count);
    output_pdo.reserve(slave_count);

    // Sized for a full frame: a slave's process data can far exceed a toy 32-byte
    // buffer, and updateInput/updateOutput copy the SM length, so a small buffer
    // would overflow on real devices.
    constexpr uint32_t PDO_MAX_SIZE = 4096;
    ESI::Parser parser;

    for (const auto& config_path : expanded_slave_configs)
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

        std::unique_ptr<EmulatedESC> esc;
        std::optional<CoE::Dictionary> esi_coe;  // CoE dictionary derived from the ESI device, if any
        bool esi_coe_advertised = false;         // true => device declares a CoE mailbox (SDO on the wire)

        if (config.contains("esi"))
        {
            // Build the EEPROM image (and CoE dictionary) from a selected ESI device.
            fs::path esi_full_path = config_dir / config["esi"].get<std::string>();
            if (not fs::exists(esi_full_path))
            {
                std::cerr << "ESI file not found: " << esi_full_path << std::endl;
                return 1;
            }
            ESI::DeviceFilter filter;
            if (config.contains("device_type"))  { filter.type         = config["device_type"].get<std::string>(); }
            if (config.contains("product_code")) { filter.product_code = config["product_code"].get<uint32_t>();    }
            if (config.contains("revision_no"))  { filter.revision_no  = config["revision_no"].get<uint32_t>();     }

            try
            {
                ESI::Device dev = parser.loadDevice(esi_full_path.string(), filter);
                CoE::materializeStorage(dev.dictionary);

                esc = std::make_unique<EmulatedESC>();
                esc->loadEeprom(ESI::buildEepromImage(dev));

                if (not dev.dictionary.empty())
                {
                    esi_coe = std::move(dev.dictionary);
                    // A mailboxless terminal (e.g. a digital I/O like EL1004) still gets its OD,
                    // but only a device that declares a CoE mailbox is reachable by SDO.
                    esi_coe_advertised = (dev.mailbox and dev.mailbox->coe);
                }
            }
            catch (std::exception const& e)
            {
                std::cerr << "Failed to build EEPROM from ESI " << esi_full_path << ": " << e.what() << std::endl;
                return 1;
            }
        }
        else if (config.contains("eeprom"))
        {
            fs::path eeprom_full_path = config_dir / config["eeprom"].get<std::string>();
            if (not fs::exists(eeprom_full_path))
            {
                std::cerr << "EEPROM file not found: " << eeprom_full_path << std::endl;
                return 1;
            }
            esc = std::make_unique<EmulatedESC>(eeprom_full_path.string().c_str());
        }
        else
        {
            std::cerr << "Config file " << config_path << " missing 'eeprom' or 'esi' field" << std::endl;
            return 1;
        }

        auto pdo = std::make_unique<PDO>(esc.get());
        auto slave = std::make_unique<Slave>(esc.get(), pdo.get());

        // The OD (owned here in `dictionaries`) is injected into the slave always, and into a
        // mailbox only if the device advertises CoE - so a mailboxless terminal still maps PDOs.
        if (esi_coe)
        {
            dictionaries.push_back(std::make_unique<CoE::Dictionary>(std::move(*esi_coe)));
            CoE::Dictionary* dictionary = dictionaries.back().get();
            slave->setDictionary(dictionary);
            if (esi_coe_advertised)
            {
                auto mbx = std::make_unique<mailbox::response::Mailbox>(esc.get(), 1024);
                mbx->enableCoE(*dictionary);
                slave->setMailbox(mbx.get());
                mailboxes.push_back(std::move(mbx));
            }
        }
        else if (config.contains("coe_xml"))
        {
            std::string coe_xml_path = config["coe_xml"];
            fs::path coe_xml_full_path = config_dir / coe_xml_path;
            if (not fs::exists(coe_xml_full_path))
            {
                std::cerr << "CoE XML file not found: " << coe_xml_full_path << std::endl;
                return 1;
            }
            dictionaries.push_back(std::make_unique<CoE::Dictionary>(parser.loadFile(coe_xml_full_path.string())));
            CoE::Dictionary* dictionary = dictionaries.back().get();
            auto mbx = std::make_unique<mailbox::response::Mailbox>(esc.get(), 1024);
            mbx->enableCoE(*dictionary);
            slave->setDictionary(dictionary);
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

    // Physical layer: route frames through the slaves in real EtherCAT order and
    // let the engine derive DL_STATUS from the wiring. Default is a daisy chain.
    std::vector<EmulatedESC*> esc_ptrs;
    esc_ptrs.reserve(escs.size());
    for (auto& esc : escs)
    {
        esc_ptrs.push_back(esc.get());
    }
    EmulatedNetwork network(std::move(esc_ptrs));

    bool redundancy = not redundancy_interface.empty();
    if (redundancy and not escs.empty())
    {
        // The redundant master port closes the ring on the tail slave's open port.
        network.setRedundancyInjection(escs.size() - 1, 1);
    }

    // Wire to break/heal on SIGUSR1/SIGUSR2 (defaults to the first downstream link).
    size_t break_a = 0;
    size_t break_b = 1;
    if (break_link.size() == 2)
    {
        break_a = static_cast<size_t>(break_link[0]);
        break_b = static_cast<size_t>(break_link[1]);
    }

    printf("Start EtherCAT network simulator on %s with %zu slaves\n", interface.c_str(), escs.size());
    if (redundancy)
    {
        printf("Cable redundancy enabled on %s (SIGUSR1 breaks / SIGUSR2 heals link %zu-%zu)\n",
               redundancy_interface.c_str(), break_a, break_b);
    }
    auto [socket, socket_redundancy] = createSockets(interface, redundancy_interface);
    // Idle wake-up so SIGINT/SIGTERM is honored. With redundancy both sockets are
    // polled every loop, so keep the timeout short to stay responsive to the master
    // (which reads the cross-over port within its own timeout).
    nanoseconds read_timeout = 100ms;
    if (redundancy)
    {
        read_timeout = 1ms;
    }
    socket->setTimeout(read_timeout);
    if (redundancy)
    {
        socket_redundancy->setTimeout(read_timeout);
    }

    std::vector<nanoseconds> stats;
    stats.reserve(1000);

    for (auto& slave : slaves)
    {
        slave->start();
    }

    uint32_t iteration_counter = 0;
    uint8_t current_value = 0x11;
    constexpr uint32_t ITER = 1000;

    int exit_code = 0;   // set non-zero on a fatal frame-write error so callers can detect it

    // Route one frame received on `in` and send the response the way the physical
    // layer would: out the opposite master port when the ring is intact, looped back
    // to the same port when the segment is broken. `redundant_path` picks the tail
    // injection order. Returns true if a frame was actually serviced.
    auto serviceFrame = [&](AbstractSocket* in, AbstractSocket* opposite, bool redundant_path)
    {
        Frame frame;
        int32_t n = in->read(frame.data(), ETH_MAX_SIZE);
        if (n <= 0)
        {
            return false;
        }
        network.route(frame, redundant_path);

        AbstractSocket* out = in;            // broken segment: loop back to the same port
        if (network.ringIntact())
        {
            out = opposite;                  // intact ring: leave by the opposite port
        }
        if (out->write(frame.data(), n) < 0)
        {
            printf("Write back frame: something wrong happened. Aborting...\n");
            exit_code = -2;
            running = 0;
        }
        return true;
    };

    while (running)
    {
        if (link_event != 0)
        {
            bool up = (link_event == 2);
            link_event = 0;
            network.setLinkState(break_a, break_b, up);
            char const* action = "broken";
            if (up)
            {
                action = "healed";
            }
            printf("Link %zu-%zu %s\n", break_a, break_b, action);
        }

        auto t1 = since_epoch();

        bool serviced = serviceFrame(socket.get(), socket_redundancy.get(), false);
        if (redundancy)
        {
            serviced |= serviceFrame(socket_redundancy.get(), socket.get(), true);
        }
        if (not serviced)
        {
            continue;  // both ports idle: re-check `running` (shutdown) and retry
        }

        for (size_t i = 0; i < slaves.size(); ++i)
        {
            slaves[i]->routine();
            if (slaves[i]->state() == State::SAFE_OP)
            {
                // Re-validate every cycle the master is delivering output data (any
                // byte differs from the 0xFF init) - not once - so the slave recovers
                // if it drops back from OP (e.g. a transient process-data watchdog).
                // Input-only slaves reach OP slave-side without this.
                bool const written = std::any_of(output_pdo[i].begin(), output_pdo[i].end(),
                    [](uint8_t b) { return b != 0xFF; });
                if (written)
                {
                    slaves[i]->validateOutputData();
                }
            }

            // Placeholder demo input (not a real process), advanced below.
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

    return exit_code;
}
