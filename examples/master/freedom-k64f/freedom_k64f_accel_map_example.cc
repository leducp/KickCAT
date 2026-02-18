#include <iostream>
#include <argparse/argparse.hpp>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"

using namespace kickcat;

// PDO mapping configuration
namespace pdo
{
    // TxPDO mapping (slave -> master): Accelerometer only
    constexpr uint32_t tx_mapping[] = 
    {
        0x60000010, // accel_x, 16 bits
        0x60010010, // accel_y, 16 bits
        0x60020010  // accel_z, 16 bits
    };
    constexpr uint32_t tx_mapping_count = sizeof(tx_mapping) / sizeof(tx_mapping[0]);

    // RxPDO mapping (master -> slave): RGB LEDs
    constexpr uint32_t rx_mapping[] = 
    {
        0x70000008, // LED_R, 8 bits
        0x70010008, // LED_G, 8 bits
        0x70020008  // LED_B, 8 bits
    };
    constexpr uint32_t rx_mapping_count = sizeof(rx_mapping) / sizeof(rx_mapping[0]);
}

namespace freedom
{
    struct fxos8700cq_accel_only
    {
        int16_t accelerometerX; // mapped 0x6000
        int16_t accelerometerY; // mapped 0x6001
        int16_t accelerometerZ; // mapped 0x6002
    } __attribute__((packed));

    struct Input
    {
        fxos8700cq_accel_only sensor;
    } __attribute__((packed));

    struct Output
    {
        uint8_t LED_R; // mapped 0x7000
        uint8_t LED_G; // mapped 0x7001
        uint8_t LED_B; // mapped 0x7002
    } __attribute__((packed));
}

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("freedom_k64f_example");

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

    auto print_current_state = [&]()
    {
        for (auto &slave : bus.slaves())
        {
            State state = bus.getCurrentState(slave);
            printf("Slave %d state is %s\n", slave.address, toString(state));
        }
    };

    uint8_t io_buffer[2048];

    const auto mapPDO = [&](const uint8_t slaveId, const uint16_t PDO_map, uint32_t const *mapping, uint8_t mapping_count, const uint32_t SM_map) -> void
    {
        auto &slave = bus.slaves().at(slaveId);
        uint8_t zeroU8 = 0;

        // Unmap previous registers, setting 0 in PDO_MAP subindex 0
        bus.writeSDO(slave, PDO_map, 0, Bus::Access::PARTIAL, &zeroU8, sizeof(zeroU8));
        // Modify mapping, setting register address in PDO's subindexes
        for (uint8_t i = 0; i < mapping_count; ++i)
        {
            bus.writeSDO(slave, PDO_map, i + 1, Bus::Access::PARTIAL, mapping + i, sizeof(uint32_t));
        }
        // Enable mapping by setting number of registers in PDO_MAP subindex 0
        bus.writeSDO(slave, PDO_map, 0, Bus::Access::PARTIAL, &mapping_count, sizeof(mapping_count));

        // Set PDO mapping to SM
        // Unmap previous mappings, setting 0 in SM_MAP subindex 0
        bus.writeSDO(slave, SM_map, 0, Bus::Access::PARTIAL, &zeroU8, sizeof(zeroU8));
        // Write first mapping (PDO_map) address in SM_MAP subindex 1
        bus.writeSDO(slave, SM_map, 1, Bus::Access::PARTIAL, &PDO_map, sizeof(PDO_map));
        // Save mapping count in SM (here only one PDO_MAP)
        uint8_t pdoMapSize = 1;
        bus.writeSDO(slave, SM_map, 0, Bus::Access::PARTIAL, &pdoMapSize, sizeof(pdoMapSize));
    };

    try
    {
        bus.init(100ms);
        printf("Init done \n");
        print_current_state();

        printf("\n=== Configuring PDO Mappings (Accelerometer Only) ===\n");

        // Map TxPDO (slave -> master): Accelerometer data
        mapPDO(0, 0x1A00, pdo::tx_mapping, pdo::tx_mapping_count, 0x1C13);

        // Map RxPDO (master -> slave): LED control
        mapPDO(0, 0x1600, pdo::rx_mapping, pdo::rx_mapping_count, 0x1C12);

        printf("=== PDO Configuration Complete ===\n\n");

        bus.createMapping(io_buffer);

        bus.enableIRQ(EcatEvent::DL_STATUS,
                      [&]()
                      {
                          printf("DL_STATUS IRQ triggered!\n");
                          bus.sendGetDLStatus(bus.slaves().at(0), [](DatagramState const &state)
                                              { printf("IRQ reset error: %s\n", toString(state)); });
                          bus.processAwaitingFrames();
                          printf("Slave DL status: %s", toString(bus.slaves().at(0).dl_status).c_str());
                      });
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

    for (auto &slave : bus.slaves())
    {
        printInfo(slave);
        printESC(slave);
    }

    auto &easycat = bus.slaves().at(0);

    try
    {
        auto cyclic_process_data = [&]()
        {
            auto noop = [](DatagramState const &) {};
            bus.processDataRead(noop);
            bus.processDataWrite(noop);
        };

        printf("Going to SAFE OP\n");
        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

        // Set valid output to exit safe op
        for (int32_t i = 0; i < easycat.output.bsize; ++i)
        {
            easycat.output.data[i] = 0xBB;
        }
        cyclic_process_data();

        printf("Going to OPERATIONAL\n");
        bus.requestState(kickcat::State::OPERATIONAL);
        bus.waitForState(kickcat::State::OPERATIONAL, 1s, cyclic_process_data);
        print_current_state();
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

    auto callback_error = [](DatagramState const &)
    { THROW_ERROR("something bad happened"); };
    link->setTimeout(10ms);

    // Map PDO memory to structs
    freedom::Input *input = reinterpret_cast<freedom::Input *>(easycat.input.data);
    freedom::Output *output = reinterpret_cast<freedom::Output *>(easycat.output.data);

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h

    // Initialize outputs
    output->LED_R = 0;
    output->LED_G = 0;
    output->LED_B = 0;

    constexpr int16_t THRESHOLD_ACCEL = 1000;

    printf("\n=== Starting Cyclic Operation (Accelerometer-based LED control) ===\n");

    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(4ms);

        try
        {
            bus.sendLogicalRead(callback_error);
            bus.sendLogicalWrite(callback_error);
            bus.sendRefreshErrorCounters(callback_error);
            bus.sendMailboxesReadChecks(callback_error);
            bus.sendMailboxesWriteChecks(callback_error);
            bus.sendReadMessages(callback_error);
            bus.sendWriteMessages(callback_error);
            bus.finalizeDatagrams();
            bus.processAwaitingFrames();

            // Read accelerometer data
            int16_t ax = input->sensor.accelerometerX;
            int16_t ay = input->sensor.accelerometerY;
            int16_t az = input->sensor.accelerometerZ;

            // LED control based on accelerometer thresholds
            output->LED_R = (ax > THRESHOLD_ACCEL || ax < -THRESHOLD_ACCEL) ? 1 : 0;
            output->LED_G = (ay > THRESHOLD_ACCEL || ay < -THRESHOLD_ACCEL) ? 1 : 0;
            output->LED_B = (az > THRESHOLD_ACCEL || az < -THRESHOLD_ACCEL) ? 1 : 0;

            printf("Accel [X:%6d Y:%6d Z:%6d] | LED [R:%u G:%u B:%u]\n",
                   ax, ay, az,
                   output->LED_R, output->LED_G, output->LED_B);
        }
        catch (std::exception const &e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    return 0;
}
