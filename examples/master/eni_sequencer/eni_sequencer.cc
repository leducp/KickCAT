#include <iostream>
#include <memory>
#include <vector>

#include <argparse/argparse.hpp>

#include "kickcat/Bus.h"
#include "kickcat/CoE/mailbox/response.h"
#include "kickcat/ENI/Parser.h"
#include "kickcat/ENI/Sequencer.h"
#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/ESI/Parser.h"
#include "kickcat/ESI/SIIBuilder.h"
#include "kickcat/Link.h"
#include "kickcat/LoopbackSocket.h"
#include "kickcat/PDO.h"
#include "kickcat/SocketNull.h"
#include "kickcat/slave/Slave.h"

using namespace kickcat;

namespace
{
    constexpr uint32_t SLAVE_PD_SIZE = 4096;

    struct SimulatedSlave
    {
        SimulatedSlave(ESI::Device device)
            : dev(std::move(device))
            , pdo(&esc)
            , sl(&esc, &pdo)
            , inputs(SLAVE_PD_SIZE, 0)
            , outputs(SLAVE_PD_SIZE, 0)
        {
            CoE::materializeStorage(dev.dictionary);
            esc.loadEeprom(ESI::buildEepromImage(dev));
            if (dev.mailbox and dev.mailbox->coe)
            {
                mbx = std::make_unique<mailbox::response::Mailbox>(&esc, 1024);
                mbx->enableCoE(dev.dictionary);
                sl.setMailbox(mbx.get());
                sl.setDictionary(&dev.dictionary);
            }
            pdo.setInput(inputs.data(), SLAVE_PD_SIZE);
            pdo.setOutput(outputs.data(), SLAVE_PD_SIZE);
            sl.start();
        }

        ESI::Device dev;
        EmulatedESC esc;
        PDO pdo;
        slave::Slave sl;
        std::unique_ptr<mailbox::response::Mailbox> mbx;
        std::vector<uint8_t> inputs;
        std::vector<uint8_t> outputs;
    };
}

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("eni_sequencer");

    std::string eni_file;
    program.add_argument("-n", "--eni")
        .help("ENI XML file describing the network")
        .required()
        .store_into(eni_file);

    std::string esi_file;
    program.add_argument("-e", "--esi")
        .help("ESI XML file used to emulate every ENI slave (matched by product code)")
        .required()
        .store_into(esi_file);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl << program;
        return 1;
    }

    ENI::Config config;
    std::vector<std::unique_ptr<SimulatedSlave>> sim;
    try
    {
        config = ENI::loadFile(eni_file);
        ESI::Parser parser;
        for (auto const& eni_slave : config.slaves)
        {
            ESI::DeviceFilter filter;
            filter.product_code = eni_slave.info.product_code;
            sim.push_back(std::make_unique<SimulatedSlave>(parser.loadDevice(esi_file, filter)));
        }
    }
    catch (std::exception const& e)
    {
        std::cerr << "Cannot build the simulated network: " << e.what() << std::endl;
        return 1;
    }

    bool outputs_valid = false;
    auto tick = [&]()
    {
        for (auto& s : sim)
        {
            s->sl.routine();
            if (outputs_valid and s->sl.state() == State::SAFE_OP)
            {
                s->sl.validateOutputData();
            }
        }
    };

    std::vector<EmulatedESC*> escs;
    for (auto& s : sim)
    {
        escs.push_back(&s->esc);
    }
    auto link = std::make_shared<Link>(std::make_shared<LoopbackSocket>(escs, tick), std::make_shared<SocketNull>(), [](){});
    Bus bus(link);

    int cyclic_errors = 0;
    auto cyclic = [&]()
    {
        bus.processDataReadWrite([&](DatagramState const&) { ++cyclic_errors; });
    };

    std::vector<uint8_t> iomap;
    try
    {
        ENI::Sequencer sequencer(bus, link, config);
        sequencer.requestState(State::SAFE_OP);

        iomap.resize(sequencer.processImageSize());
        sequencer.mapProcessImage(iomap.data(), iomap.size());
        cyclic();

        outputs_valid = true;
        sequencer.requestState(State::OPERATIONAL, cyclic);
    }
    catch (std::exception const& e)
    {
        std::cerr << "ENI sequence failed: " << e.what() << std::endl;
        return 1;
    }

    // The period the ENI frames were planned for, when the configurator gave one.
    nanoseconds period = 1ms;
    if ((not config.cyclic.empty()) and config.cyclic.front().cycle_time)
    {
        period = *config.cyclic.front().cycle_time;
    }
    printf("cycle period: %lld us\n", static_cast<long long>(duration_cast<microseconds>(period).count()));
    for (int i = 0; i < 100; ++i)
    {
        cyclic();
        sleep(period);
    }
    for (std::size_t i = 0; i < bus.slaves().size(); ++i)
    {
        Slave const& slave = bus.slaves()[i];
        printf("slave %zu: station %u, state 0x%02x, inputs 0x%05x/%d, outputs 0x%05x/%d\n", i, slave.address,
            static_cast<unsigned>(sim[i]->sl.state()), slave.input.address, slave.input.bsize, slave.output.address, slave.output.bsize);
    }
    printf("cyclic errors: %d\n", cyclic_errors);

    if (cyclic_errors != 0)
    {
        return 1;
    }
    return 0;
}
