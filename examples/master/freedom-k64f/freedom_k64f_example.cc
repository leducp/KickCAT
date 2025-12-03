#include <iostream>
#include <vector>
#include <cstring>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"

using namespace kickcat;

namespace freedom
{
    struct fxos8700cq
    {
        int16_t accelerometerX; // mapped 0x6000
        int16_t accelerometerY; // mapped 0x6001
        int16_t accelerometerZ; // mapped 0x6002

        int16_t magnetometerX; // mapped 0x6003
        int16_t magnetometerY; // mapped 0x6004
        int16_t magnetometerZ; // mapped 0x6005
    } __attribute__((packed));

    struct Input
    {
        fxos8700cq sensor;
    } __attribute__((packed));

    struct Output
    {
        uint8_t LED_R; // mapped 0x7000
        uint8_t LED_G; // mapped 0x7001
        uint8_t LED_B; // mapped 0x7002
    } __attribute__((packed));
}

// Structure to hold PDO mapping information
struct PDOObjectMapping
{
    uint16_t index;
    uint8_t subindex;
    uint8_t bit_length;
    uint32_t byte_offset; // offset in contiguous buffer
};

struct PDOLayout
{
    std::vector<PDOObjectMapping> rx_objects; // outputs (master->slave)
    std::vector<PDOObjectMapping> tx_objects; // inputs (slave->master)
    uint32_t rx_total_bytes{0};
    uint32_t tx_total_bytes{0};
};

// Function to read PDO configuration from slave
PDOLayout readPDOConfiguration(Bus &bus, Slave &slave)
{
    PDOLayout layout;

    printf("\n=== Reading PDO Configuration ===\n");

    // Read RxPDO assignment (0x1C12 - outputs from master)
    {
        uint8_t num_rx_pdos = 0;
        uint32_t size = sizeof(num_rx_pdos);

        try
        {
            bus.readSDO(slave, 0x1C12, 0, Bus::Access::PARTIAL, &num_rx_pdos, &size, 100ms);
            printf("Number of RxPDOs (0x1C12): %u\n", num_rx_pdos);

            uint32_t byte_offset = 0;

            for (uint8_t i = 1; i <= num_rx_pdos; ++i)
            {
                uint16_t pdo_index = 0;
                size = sizeof(pdo_index);
                bus.readSDO(slave, 0x1C12, i, Bus::Access::PARTIAL, &pdo_index, &size, 100ms);
                printf("  RxPDO %u: 0x%04X\n", i, pdo_index);

                // Read number of mapped objects in this PDO
                uint8_t num_objects = 0;
                size = sizeof(num_objects);
                bus.readSDO(slave, pdo_index, 0, Bus::Access::PARTIAL, &num_objects, &size, 100ms);
                printf("    Number of objects: %u\n", num_objects);

                // Read each mapped object
                for (uint8_t j = 1; j <= num_objects; ++j)
                {
                    uint32_t mapping_entry = 0;
                    size = sizeof(mapping_entry);
                    bus.readSDO(slave, pdo_index, j, Bus::Access::PARTIAL, &mapping_entry, &size, 100ms);

                    // Decode mapping entry: [Index:16][SubIndex:8][BitLength:8]
                    uint16_t obj_index = (mapping_entry >> 16) & 0xFFFF;
                    uint8_t obj_subindex = (mapping_entry >> 8) & 0xFF;
                    uint8_t obj_bitlength = mapping_entry & 0xFF;

                    PDOObjectMapping obj;
                    obj.index = obj_index;
                    obj.subindex = obj_subindex;
                    obj.bit_length = obj_bitlength;
                    obj.byte_offset = byte_offset;

                    layout.rx_objects.push_back(obj);

                    uint32_t byte_length = (obj_bitlength + 7) / 8;
                    byte_offset += byte_length;

                    printf("      [%u] Index: 0x%04X:%u, Bits: %u, Offset: %u\n",
                           j, obj_index, obj_subindex, obj_bitlength, obj.byte_offset);
                }
            }

            layout.rx_total_bytes = byte_offset;
            printf("  Total RxPDO size: %u bytes\n", layout.rx_total_bytes);
        }
        catch (std::exception const &e)
        {
            printf("Error reading RxPDO config: %s\n", e.what());
        }
    }

    // Read TxPDO assignment (0x1C13 - inputs to master)
    {
        uint8_t num_tx_pdos = 0;
        uint32_t size = sizeof(num_tx_pdos);

        try
        {
            bus.readSDO(slave, 0x1C13, 0, Bus::Access::PARTIAL, &num_tx_pdos, &size, 100ms);
            printf("Number of TxPDOs (0x1C13): %u\n", num_tx_pdos);

            uint32_t byte_offset = 0;

            for (uint8_t i = 1; i <= num_tx_pdos; ++i)
            {
                uint16_t pdo_index = 0;
                size = sizeof(pdo_index);
                bus.readSDO(slave, 0x1C13, i, Bus::Access::PARTIAL, &pdo_index, &size, 100ms);
                printf("  TxPDO %u: 0x%04X\n", i, pdo_index);

                // Read number of mapped objects in this PDO
                uint8_t num_objects = 0;
                size = sizeof(num_objects);
                bus.readSDO(slave, pdo_index, 0, Bus::Access::PARTIAL, &num_objects, &size, 100ms);
                printf("    Number of objects: %u\n", num_objects);

                // Read each mapped object
                for (uint8_t j = 1; j <= num_objects; ++j)
                {
                    uint32_t mapping_entry = 0;
                    size = sizeof(mapping_entry);
                    bus.readSDO(slave, pdo_index, j, Bus::Access::PARTIAL, &mapping_entry, &size, 100ms);

                    // Decode mapping entry
                    uint16_t obj_index = (mapping_entry >> 16) & 0xFFFF;
                    uint8_t obj_subindex = (mapping_entry >> 8) & 0xFF;
                    uint8_t obj_bitlength = mapping_entry & 0xFF;

                    PDOObjectMapping obj;
                    obj.index = obj_index;
                    obj.subindex = obj_subindex;
                    obj.bit_length = obj_bitlength;
                    obj.byte_offset = byte_offset;

                    layout.tx_objects.push_back(obj);

                    uint32_t byte_length = (obj_bitlength + 7) / 8;
                    byte_offset += byte_length;

                    printf("      [%u] Index: 0x%04X:%u, Bits: %u, Offset: %u\n",
                           j, obj_index, obj_subindex, obj_bitlength, obj.byte_offset);
                }
            }

            layout.tx_total_bytes = byte_offset;
            printf("  Total TxPDO size: %u bytes\n", layout.tx_total_bytes);
        }
        catch (std::exception const &e)
        {
            printf("Error reading TxPDO config: %s\n", e.what());
        }
    }

    printf("=== PDO Configuration Complete ===\n\n");
    return layout;
}

int main(int argc, char *argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
    }

    std::string red_interface_name = "";
    std::string nom_interface_name = argv[1];
    if (argc == 3)
    {
        red_interface_name = argv[2];
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
    PDOLayout pdo_layout;

    try
    {
        bus.init(100ms);
        printf("Init done \n");
        print_current_state();

        // Read PDO configuration at PreOp
        auto &easycat = bus.slaves().at(0);
        pdo_layout = readPDOConfiguration(bus, easycat);

        // Verify layout matches expected structure
        printf("\n=== Verification ===\n");
        printf("Expected Input size: %lu bytes\n", sizeof(freedom::Input));
        printf("Detected TxPDO size: %u bytes\n", pdo_layout.tx_total_bytes);
        printf("Expected Output size: %lu bytes\n", sizeof(freedom::Output));
        printf("Detected RxPDO size: %u bytes\n", pdo_layout.rx_total_bytes);

        // Continue with normal mapping
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

        // Set valid output to exit safe op.
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

    auto callback_error = [](DatagramState const &)
    { THROW_ERROR("something bad happened"); };
    link->setTimeout(10ms); // Adapt to your use case (RT loop)

    // Map PDO memory to structs
    freedom::Input *input = reinterpret_cast<freedom::Input *>(easycat.input.data);
    freedom::Output *output = reinterpret_cast<freedom::Output *>(easycat.output.data);

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h

    for (int32_t i = 0; i < easycat.output.bsize; ++i)
    {
        easycat.output.data[i] = 0xAA;
    }

    constexpr int16_t THRESHOLD_ACCEL = 1000;
    constexpr int16_t THRESHOLD_MAG = 1000;

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

            int16_t ax = input->sensor.accelerometerX;
            int16_t ay = input->sensor.accelerometerY;
            int16_t az = input->sensor.accelerometerZ;

            int16_t mx = input->sensor.magnetometerX;
            int16_t my = input->sensor.magnetometerY;
            int16_t mz = input->sensor.magnetometerZ;

            output->LED_R = (ax > THRESHOLD_ACCEL || ax < -THRESHOLD_ACCEL) ? 1 : 0;
            output->LED_G = (ay > THRESHOLD_ACCEL || ay < -THRESHOLD_ACCEL) ? 1 : 0;
            output->LED_B = (mz > THRESHOLD_MAG || mz < -THRESHOLD_MAG) ? 1 : 0;

            printf("Accel [X:%d Y:%d Z:%d] | Mag [X:%d Y:%d Z:%d] | LED [R:%u G:%u B:%u]\n",
                   ax, ay, az,
                   mx, my, mz,
                   output->LED_R, output->LED_G, output->LED_B);
        }
        catch (std::exception const &e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    return 0;
}
