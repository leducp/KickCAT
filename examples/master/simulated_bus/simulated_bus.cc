// Spin up ONE emulated EtherCAT slave from an ESI device and drive a real master
// Bus against it, entirely in-process: no network interface, no /dev/shm, no
// second process. This is the simplest way to exercise the kickcat master and
// slave stacks together, and the recommended starting point for experimenting
// with the simulator. The in-process transport is kickcat::LoopbackSocket
// (lib/include/kickcat/LoopbackSocket.h): each master frame is run through the
// emulated slave and the slave is "ticked" once.
//
//   simulated_bus -f "Beckhoff EL1xxx.xml" -t EL1008
//
#include <iostream>
#include <memory>
#include <vector>

#include <argparse/argparse.hpp>

#include "kickcat/Bus.h"
#include "kickcat/CoE/OD.h"
#include "kickcat/CoE/mailbox/response.h"
#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/ESI/Parser.h"
#include "kickcat/ESI/SIIBuilder.h"
#include "kickcat/Link.h"
#include "kickcat/LoopbackSocket.h"
#include "kickcat/PDO.h"
#include "kickcat/SocketNull.h"
#include "kickcat/slave/Slave.h"

using namespace kickcat;

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("simulated_bus");

    std::string esi_file;
    program.add_argument("-f", "--file")
        .help("ESI XML file describing the slave")
        .required()
        .store_into(esi_file);

    std::string device_type;
    program.add_argument("-t", "--type")
        .help("device type to select (e.g. EL1008); default: first device in the file")
        .store_into(device_type);

    bool use_dc = false;
    program.add_argument("--dc")
        .help("run Distributed Clocks initialization (1 ms cycle) before going OP")
        .flag()
        .store_into(use_dc);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl << program;
        return 1;
    }

    // 1. Parse the ESI and compile the selected device into an emulated slave's
    //    EEPROM image plus its CoE object dictionary.
    ESI::Parser parser;
    ESI::DeviceFilter filter;
    if (not device_type.empty())
    {
        filter.type = device_type;
    }

    ESI::Device dev;
    try
    {
        dev = parser.loadDevice(esi_file, filter);
    }
    catch (std::exception const& e)
    {
        std::cerr << "Cannot load device: " << e.what() << std::endl;
        return 1;
    }
    CoE::materializeStorage(dev.dictionary);
    printf("Device: %s  (product 0x%08x, revision 0x%x)\n", dev.type.c_str(), dev.product_code, dev.revision_no);

    // 2. Assemble the emulated slave: hardware (EmulatedESC) + process data (PDO)
    //    + state machine (Slave) + optional CoE mailbox. Same recipe as the
    //    network simulator, just kept in local variables here.
    EmulatedESC esc;
    esc.loadEeprom(ESI::buildEepromImage(dev));

    PDO pdo(&esc);
    slave::Slave sl(&esc, &pdo);

    std::unique_ptr<mailbox::response::Mailbox> mbx;
    if (dev.mailbox and dev.mailbox->coe)
    {
        mbx = std::make_unique<mailbox::response::Mailbox>(&esc, 1024);
        mbx->enableCoE(std::move(dev.dictionary));
        sl.setMailbox(mbx.get());
    }

    constexpr uint32_t PDO_SIZE = 4096;  // a full frame: never overflow on large PDOs
    std::vector<uint8_t> inputs(PDO_SIZE, 0);
    std::vector<uint8_t> outputs(PDO_SIZE, 0);
    pdo.setInput(inputs.data(), PDO_SIZE);
    pdo.setOutput(outputs.data(), PDO_SIZE);

    uint16_t dl_status = (1 << 4) | (1 << 9);  // single slave: only port 0 is connected
    esc.write(reg::ESC_DL_STATUS, &dl_status, sizeof(dl_status));
    sl.start();

    // 3. Wire a master Bus to the slave through the in-process loopback. The tick
    //    advances the slave once per frame; once the master is driving process
    //    data we declare outputs valid so the slave may leave SAFE_OP.
    bool op_phase = false;
    auto tick = [&]()
    {
        sl.routine();
        if (op_phase and sl.state() == State::SAFE_OP)
        {
            sl.validateOutputData();
        }
    };

    auto loopback = std::make_shared<LoopbackSocket>(std::vector<EmulatedESC*>{&esc}, tick);
    auto link = std::make_shared<Link>(loopback, std::make_shared<SocketNull>(), [](){});
    Bus bus(link);

    auto noop   = [](DatagramState const&) {};
    auto cyclic = [&]()
    {
        bus.processDataRead(noop);
        bus.processDataWrite(noop);
    };

    // 4. Drive the EtherCAT state machine to OPERATIONAL.
    std::vector<uint8_t> iomap(4096);  // master-side process image
    try
    {
        bus.init(100ms);
        printf("Detected %zu slave(s)\n", bus.slaves().size());
        bus.createMapping(iomap.data(), iomap.size());

        if (use_dc)
        {
            bus.enableDC(1ms, 0ns, 0ns);
            char const* synced = "no";
            if (bus.isDCSynchronized())
            {
                synced = "yes";
            }
            printf("DC initialized, synchronized: %s\n", synced);
        }

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 500ms);
        printf("SAFE_OP reached\n");

        for (auto& s : bus.slaves())
        {
            for (int32_t i = 0; i < s.output.bsize; ++i)
            {
                s.output.data[i] = 0x55;
            }
        }
        cyclic();

        op_phase = true;
        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 500ms, cyclic);
        printf("OPERATIONAL reached\n");
    }
    catch (std::exception const& e)
    {
        printf("Could not reach OPERATIONAL (slave state 0x%02x): %s\n",
            static_cast<unsigned>(sl.state()), e.what());
        return 1;
    }

    // 5. Exchange a few process-data cycles and report the mapped sizes.
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        cyclic();
    }
    auto const& s0 = bus.slaves().at(0);
    printf("Process data: %d input byte(s), %d output byte(s)\n", s0.input.bsize, s0.output.bsize);

    return 0;
}
