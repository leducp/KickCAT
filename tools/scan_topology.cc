#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>

#include "kickcat/Bus.h"
#include "kickcat/Diagnostics.h"
#include "kickcat/Link.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"

using namespace kickcat;

namespace
{
    std::string hex(uint32_t value, int width)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "0x%0*x", width, value);
        return buffer;
    }

    std::string pad(std::string const& text, std::size_t width)
    {
        if (text.size() >= width)
        {
            return text;
        }
        return text + std::string(width - text.size(), ' ');
    }

    std::string mailboxProtocols(uint16_t mask)
    {
        std::string protocols;
        auto add = [&](uint16_t bit, char const* name)
        {
            if (mask & bit)
            {
                if (not protocols.empty()) { protocols += "+"; }
                protocols += name;
            }
        };

        add(eeprom::MailboxProtocol::AoE, "AoE");
        add(eeprom::MailboxProtocol::CoE, "CoE");
        add(eeprom::MailboxProtocol::EoE, "EoE");
        add(eeprom::MailboxProtocol::FoE, "FoE");
        add(eeprom::MailboxProtocol::SoE, "SoE");

        return protocols.empty() ? "-" : protocols;
    }

    std::string openPorts(Slave const& slave)
    {
        std::string ports;
        for (int port = 0; port < 4; ++port)
        {
            if (slave.dl_status.communication(port))
            {
                if (not ports.empty()) { ports += ","; }
                ports += std::to_string(port);
            }
        }
        return ports.empty() ? "-" : ports;
    }

    int32_t pdoBits(std::vector<eeprom::PDOMapping> const& pdos)
    {
        int32_t bits = 0;
        for (auto const& mapping : pdos)
        {
            for (auto const& entry : mapping.entries)
            {
                bits += entry.bitlen;
            }
        }
        return bits;
    }

    std::string stateLabel(Slave const& slave)
    {
        std::string label = toShortString(static_cast<State>(slave.al_status));
        if (slave.al_status & State::ERROR_ACK)
        {
            label += "+ERR";
        }
        return label;
    }

    /// \return the non-zero counters as one line, empty if the slave is clean
    std::string errorDigest(Slave const& slave)
    {
        auto const& counters = slave.error_counters;
        std::string digest;
        auto add = [&](std::string const& name, int value)
        {
            if (value == 0) { return; }
            if (not digest.empty()) { digest += " "; }
            digest += name + "=" + std::to_string(value);
        };

        for (int port = 0; port < 4; ++port)
        {
            std::string prefix = "p" + std::to_string(port) + ".";
            add(prefix + "invalid_frame",  counters.rx[port].invalid_frame);
            add(prefix + "physical_layer", counters.rx[port].physical_layer);
            add(prefix + "forwarded",      counters.forwarded[port]);
            add(prefix + "lost_link",      counters.lost_link[port]);
        }
        add("malformed_frame", counters.malformed_frame);
        add("pdi",             counters.pdi);
        add("pdi_error_code",  counters.pdi_error_code);

        return digest;
    }

    void printIdentityTable(std::vector<Slave> const& slaves)
    {
        std::size_t name_width = 4;
        std::size_t type_width = 4;
        for (auto const& slave : slaves)
        {
            name_width = std::max(name_width, slave.name().size());
            type_width = std::max(type_width, slave.type().size());
        }

        printf("\n=== Identity ===\n");
        printf("  #  Addr  Alias   %s  %s  Vendor      Product     Revision    Serial\n",
               pad("Name", name_width).c_str(), pad("Type", type_width).c_str());

        for (std::size_t i = 0; i < slaves.size(); ++i)
        {
            auto const& slave = slaves[i];
            auto type = slave.type();
            printf("%3zu  %4u  %s  %s  %s  %s  %s  %s  %s\n",
                   i,
                   slave.address,
                   hex(slave.sii.info.station_alias, 4).c_str(),
                   pad(slave.name(), name_width).c_str(),
                   pad(type.empty() ? "-" : type, type_width).c_str(),
                   hex(slave.sii.info.vendor_id, 8).c_str(),
                   hex(slave.sii.info.product_code, 8).c_str(),
                   hex(slave.sii.info.revision_number, 8).c_str(),
                   hex(slave.sii.info.serial_number, 8).c_str());
        }
    }

    void printStatusTable(std::vector<Slave> const& slaves)
    {
        printf("\n=== Status ===\n");
        printf("  #  Addr  State     AL code  Mailbox      Mbx in/out  Tx/Rx bits  Ports  DC   eBus mA  Errors\n");

        for (std::size_t i = 0; i < slaves.size(); ++i)
        {
            auto const& slave = slaves[i];
            std::string mbx_size = std::to_string(slave.mailbox.recv_size) + "/" + std::to_string(slave.mailbox.send_size);
            std::string pdo_size = std::to_string(pdoBits(slave.sii.TxPDO)) + "/" + std::to_string(pdoBits(slave.sii.RxPDO));

            printf("%3zu  %4u  %s  %s   %s  %s  %s  %s  %s  %7d  %d\n",
                   i,
                   slave.address,
                   pad(stateLabel(slave), 8).c_str(),
                   hex(slave.al_status_code, 4).c_str(),
                   pad(mailboxProtocols(slave.sii.info.mailbox_protocol), 11).c_str(),
                   pad(mbx_size, 10).c_str(),
                   pad(pdo_size, 10).c_str(),
                   pad(openPorts(slave), 5).c_str(),
                   slave.isDCSupport() ? "yes" : "no ",
                   slave.sii.general.current_on_ebus,
                   slave.computeErrorCounters());
        }

        printf("Tx/Rx bits: process data declared by the SII PDO categories. A device offering several\n"
               "alternative mappings has all of them counted, so the figure is an upper bound.\n");
    }

    void printAlarmReport(std::vector<Slave> const& slaves, State expected)
    {
        std::vector<std::string> lines;
        for (auto const& slave : slaves)
        {
            bool wrong_state = (slave.al_status & State::MASK_STATE) != expected;
            bool error_flag  = (slave.al_status & State::ERROR_ACK) != 0;
            if (not wrong_state and not error_flag)
            {
                continue;
            }

            std::string line = "Slave " + std::to_string(slave.address) + " (" + slave.name() + "): "
                             + toString(static_cast<State>(slave.al_status));
            if (error_flag or (slave.al_status_code != 0))
            {
                line += std::string(" - ") + ALStatus_to_string(slave.al_status_code)
                      + " (" + hex(slave.al_status_code, 4) + ")";
            }
            lines.push_back(line);
        }

        if (lines.empty())
        {
            return;
        }

        printf("\n=== Slaves not in %s ===\n", toString(expected));
        for (auto const& line : lines)
        {
            printf("%s\n", line.c_str());
        }
    }

    void printErrorReport(std::vector<Slave> const& slaves)
    {
        std::vector<std::string> lines;
        for (auto const& slave : slaves)
        {
            auto digest = errorDigest(slave);
            if (not digest.empty())
            {
                lines.push_back("Slave " + std::to_string(slave.address) + " (" + slave.name() + "): " + digest);
            }
        }

        printf("\n=== Error counters ===\n");
        if (lines.empty())
        {
            printf("All counters are zero.\n");
            return;
        }
        for (auto const& line : lines)
        {
            printf("%s\n", line.c_str());
        }
    }

    void printDetails(std::vector<Slave> const& slaves, bool with_sii, bool with_esc, bool with_pdo,
                      bool with_dl, bool with_errors, int only_address)
    {
        for (auto const& slave : slaves)
        {
            if ((only_address != 0) and (slave.address != only_address))
            {
                continue;
            }

            printf("\n=============== Slave %u - %s ===============\n", slave.address, slave.name().c_str());
            if (with_sii) { printInfo(slave); }
            if (with_esc) { printESC(slave);  }
            if (with_pdo) { printPDOs(slave); }
            if (with_dl)
            {
                printf("\n **** DL Status ****\n%s", toString(slave.dl_status).c_str());
            }
            if (with_errors)
            {
                printf("\n **** Error Counters ****\n%s", toString(slave.error_counters).c_str());
            }
        }
    }
}


int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("scan_topology");
    program.add_description("Scan an EtherCAT segment: report the topology and the identity, state and link "
                            "health of every slave found on it.");

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

    bool detail_all = false;
    program.add_argument("-a", "--all")
        .help("dump every detail section of every slave (SII, ESC, PDOs, DL status, error counters)")
        .store_into(detail_all);

    bool detail_sii = false;
    program.add_argument("--sii")
        .help("dump the SII: identity, mailboxes, FMMUs and sync managers")
        .store_into(detail_sii);

    bool detail_esc = false;
    program.add_argument("--esc")
        .help("dump the ESC description: type, revision, resources, ports and features")
        .store_into(detail_esc);

    bool detail_pdo = false;
    program.add_argument("--pdo")
        .help("dump the RxPDO/TxPDO mapping declared by the SII")
        .store_into(detail_pdo);

    bool detail_dl = false;
    program.add_argument("--dl")
        .help("dump the per port DL status")
        .store_into(detail_dl);

    bool detail_errors = false;
    program.add_argument("--errors")
        .help("dump the full error counters, including the zeroed ones")
        .store_into(detail_errors);

    int only_address = 0;
    program.add_argument("-s", "--slave")
        .help("restrict the detail sections to this station address (default: every slave)")
        .default_value(0)
        .scan<'i', int>()
        .store_into(only_address);

    bool clear_errors = false;
    program.add_argument("--clear-errors")
        .help("clear the slaves error counters once the report is printed")
        .store_into(clear_errors);

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

    if (detail_all)
    {
        detail_sii    = true;
        detail_esc    = true;
        detail_pdo    = true;
        detail_dl     = true;
        detail_errors = true;
    }
    bool any_detail = detail_sii or detail_esc or detail_pdo or detail_dl or detail_errors;

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

    bool redundancy_activated = false;
    auto report_redundancy = [&redundancy_activated]()
    {
        redundancy_activated = true;
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->checkRedundancyNeeded();

    Bus bus(link);

    try
    {
        // init() brings the bus to PRE-OP and, on the way, fetches the ESC description, the DL
        // status and the whole SII of every slave: the report below is a read of that state.
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

    auto& slaves = bus.slaves();

    // Refresh what init() does not leave up to date. A datagram error is reported and skipped
    // instead of thrown: a diagnostic tool shall keep going and show what it did manage to read.
    auto report_error = [](char const* what)
    {
        return [what](DatagramState const& state)
        {
            fprintf(stderr, "Warning: %s (%s)\n", what, toString(state));
        };
    };

    for (auto& slave : slaves)
    {
        // Read the AL status directly rather than through Bus::getCurrentState: the latter throws
        // on a slave holding an error code, which would hide every other slave of the segment.
        bus.sendGetALStatus(slave, report_error("could not read AL status"));
        bus.sendGetDLStatus(slave, report_error("could not read DL status"));
    }
    bus.sendRefreshErrorCounters(report_error("could not read error counters"));
    bus.processAwaitingFrames();

    int32_t ebus_current = 0;
    for (auto const& slave : slaves)
    {
        ebus_current += slave.sii.general.current_on_ebus;
    }

    printf("\n=== Bus ===\n");
    printf("Interface:        %s\n", nom_interface_name.c_str());
    printf("Redundancy:       %s\n", red_interface_name.empty() ? "not configured"
                                   : (redundancy_activated ? "ACTIVE (cable loss)" : "configured, inactive"));
    printf("Slaves detected:  %d\n", bus.detectedSlaves());
    printf("E-bus current:    %d mA (SII declared, negative feeds the bus)\n", ebus_current);

    printIdentityTable(slaves);
    printStatusTable(slaves);
    printAlarmReport(slaves, State::PRE_OP);
    printErrorReport(slaves);

    printf("\n=== Topology ===\n");
    try
    {
        print(getTopology(slaves), slaves);
    }
    catch (std::exception const& e)
    {
        // A slave reporting no open port (a link coming up or going down mid scan) makes the
        // parent resolution fail: the report above still stands, only the tree is lost.
        fprintf(stderr, "Could not resolve the topology: %s\n", e.what());
    }

    if (any_detail)
    {
        printDetails(slaves, detail_sii, detail_esc, detail_pdo, detail_dl, detail_errors, only_address);
    }
    else
    {
        printf("\nRun with --all for the full per slave dump (--sii, --esc, --pdo, --dl, --errors for a\n"
               "single section, -s <addr> to restrict it to one slave).\n");
    }

    if (clear_errors)
    {
        try
        {
            bus.clearErrorCounters();
            printf("\nError counters cleared.\n");
        }
        catch (std::exception const& e)
        {
            std::cerr << "Could not clear the error counters: " << e.what() << std::endl;
        }
    }

    return 0;
}
