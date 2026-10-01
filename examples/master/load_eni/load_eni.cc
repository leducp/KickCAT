#include <argparse/argparse.hpp>

#include "kickcat/ENI/Parser.h"
#include "kickcat/OS/Time.h"

using namespace kickcat;

namespace
{
    template<typename T>
    void printTransitionCounts(char const* label, std::vector<T> const& cmds)
    {
        printf("    %-10s %3zu:", label, cmds.size());
        for (uint16_t bit = 0; bit < 15; ++bit)
        {
            ENI::transition::Type t = static_cast<ENI::transition::Type>(1 << bit);
            int count = 0;
            for (auto const& cmd : cmds)
            {
                if (cmd.transitions & t)
                {
                    ++count;
                }
            }
            if (count > 0)
            {
                printf(" %s=%d", ENI::transition::toString(t), count);
            }
        }
        printf("\n");
    }

    std::string statesToString(uint8_t states)
    {
        std::string out;
        if (states & ENI::state::INIT)   { out += "I"; }
        if (states & ENI::state::PREOP)  { out += "P"; }
        if (states & ENI::state::SAFEOP) { out += "S"; }
        if (states & ENI::state::OP)     { out += "O"; }
        return out;
    }

    void printSlave(std::size_t index, ENI::Slave const& slave)
    {
        ENI::SlaveInfo const& info = slave.info;
        printf("[%zu] %s\n", index, info.name.c_str());
        printf("    phys 0x%04x  autoinc 0x%04x  vendor 0x%08x  product 0x%08x  revision 0x%08x\n",
            info.phys_addr.value_or(0), info.auto_inc_addr.value_or(0), info.vendor_id, info.product_code, info.revision_no);

        uint32_t out_bits = 0;
        for (auto const& range : slave.process_data.send)
        {
            out_bits += range.bit_length;
        }
        uint32_t in_bits = 0;
        for (auto const& range : slave.process_data.recv)
        {
            in_bits += range.bit_length;
        }
        printf("    process data: %u output bits, %u input bits, %zu SM, %zu RxPdo, %zu TxPdo\n",
            out_bits, in_bits, slave.process_data.sms.size(), slave.process_data.rx_pdos.size(), slave.process_data.tx_pdos.size());

        if (slave.mailbox)
        {
            ENI::Mailbox const& mbx = *slave.mailbox;
            printf("    mailbox: send 0x%04x/%u recv 0x%04x/%u protocols 0x%02x unsupported InitCmds %u\n",
                mbx.send.start, mbx.send.length, mbx.recv.start, mbx.recv.length, mbx.protocols, mbx.unsupported_init_cmds);
            printTransitionCounts("CoE", mbx.coe_init_cmds);
        }
        printTransitionCounts("InitCmds", slave.init_cmds);

        if (slave.dc)
        {
            printf("    DC: cycle0 %ld ns shift %ld ns\n",
                static_cast<long>(slave.dc->cycle_time0.value_or(0ns).count()), static_cast<long>(slave.dc->shift_time.value_or(0ns).count()));
        }
    }
}

int main(int argc, char const* argv[])
{
    argparse::ArgumentParser program("load_eni");

    std::string eni_file;
    program.add_argument("-f", "--file")
        .help("ENI XML file")
        .required()
        .store_into(eni_file);

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

    ENI::Config config;
    nanoseconds t1 = now();
    try
    {
        config = ENI::loadFile(eni_file);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    nanoseconds t2 = now();

    ENI::Master const& master = config.master;
    printf("Master: %s  source %02x:%02x:%02x:%02x:%02x:%02x\n", master.name.c_str(),
        master.source[0], master.source[1], master.source[2], master.source[3], master.source[4], master.source[5]);
    printTransitionCounts("InitCmds", master.init_cmds);

    printf("%zu slave(s)\n", config.slaves.size());
    for (std::size_t i = 0; i < config.slaves.size(); ++i)
    {
        printSlave(i, config.slaves[i]);
    }

    for (auto const& cyclic : config.cyclic)
    {
        printf("Cyclic: %zu frame(s)\n", cyclic.frames.size());
        for (auto const& frame : cyclic.frames)
        {
            for (auto const& cmd : frame.cmds)
            {
                printf("    %-4s %-18s addr 0x%08x len %4zu wkc %u in %d out %d\n",
                    statesToString(cmd.states).c_str(), toString(cmd.cmd), cmd.address(), cmd.data.size(), cmd.wkc.value_or(0),
                    static_cast<int>(cmd.input_offs.value_or(-1)), static_cast<int>(cmd.output_offs.value_or(-1)));
            }
        }
    }

    if (config.process_image)
    {
        printf("ProcessImage: inputs %u bytes (%zu vars), outputs %u bytes (%zu vars)\n",
            config.process_image->inputs.byte_size, config.process_image->inputs.variables.size(),
            config.process_image->outputs.byte_size, config.process_image->outputs.variables.size());
    }

    printf("Parsed in %fs\n", seconds_f(t2 - t1).count());
    return 0;
}
