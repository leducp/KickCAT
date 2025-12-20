#include "kickcat/ESC/Lan9252.h"
#include "kickcat/OS/Time.h"
#include "kickcat/PDO.h"
#include "kickcat/nuttx/SPI.h"
#include "kickcat/protocol.h"
#include "kickcat/slave/Slave.h"
#include <arch/board/board.h>
#include <nuttx/board.h>
#include <nuttx/sensors/fxos8700cq.h>
#include <nuttx/leds/userled.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace kickcat;

struct SensorData
{
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t mag_x;
    int16_t mag_y;
    int16_t mag_z;
};

struct LedData
{
    uint8_t led_r;
    uint8_t led_g;
    uint8_t led_b;
};

struct PdoMappingEntry
{
    uint16_t index;     // Object index (e.g., 0x6000)
    uint8_t subindex;   // Object subindex
    uint8_t bit_length; // Size in bits

    static PdoMappingEntry parse(uint32_t mapping_value)
    {
        PdoMappingEntry entry;
        entry.index = (mapping_value >> 16) & 0xFFFF;
        entry.subindex = (mapping_value >> 8) & 0xFF;
        entry.bit_length = mapping_value & 0xFF;
        return entry;
    }
};

class DynamicPdoManager
{
public:
    DynamicPdoManager() : txpdo_size_(0), rxpdo_size_(0) {}

    // Parse TxPDO mapping from Object Dictionary
    bool parseTxPdoMapping(CoE::Dictionary &od)
    {
        txpdo_entries_.clear();
        txpdo_size_ = 0;

        // Find 0x1A00 in dictionary
        auto it = std::find_if(od.begin(), od.end(),
                               [](const CoE::Object &obj)
                               { return obj.index == 0x1A00; });

        if (it == od.end())
        {
            printf("ERROR: 0x1A00 not found in OD\n");
            return false;
        }

        // Get number of entries (subindex 0)
        if (it->entries.empty())
        {
            printf("ERROR: 0x1A00 has no entries\n");
            return false;
        }

        uint8_t num_entries = *reinterpret_cast<uint8_t *>(it->entries[0].data);
        printf("TxPDO: Found %u mapping entries\n", num_entries);

        // Parse each mapping entry
        for (uint8_t i = 1; i <= num_entries && i < it->entries.size(); ++i)
        {
            uint32_t mapping_value = *reinterpret_cast<uint32_t *>(it->entries[i].data);
            PdoMappingEntry entry = PdoMappingEntry::parse(mapping_value);

            printf("  Entry %u: 0x%04X:%02X (%u bits)\n",
                   i, entry.index, entry.subindex, entry.bit_length);

            txpdo_entries_.push_back(entry);
            txpdo_size_ += (entry.bit_length + 7) / 8; // Round up to bytes
        }

        printf("TxPDO total size: %zu bytes\n", txpdo_size_);
        return true;
    }

    // Parse RxPDO mapping from Object Dictionary
    bool parseRxPdoMapping(CoE::Dictionary &od)
    {
        rxpdo_entries_.clear();
        rxpdo_size_ = 0;

        // Find 0x1600 in dictionary
        auto it = std::find_if(od.begin(), od.end(),
                               [](const CoE::Object &obj)
                               { return obj.index == 0x1600; });

        if (it == od.end())
        {
            printf("ERROR: 0x1600 not found in OD\n");
            return false;
        }

        if (it->entries.empty())
        {
            printf("ERROR: 0x1600 has no entries\n");
            return false;
        }

        uint8_t num_entries = *reinterpret_cast<uint8_t *>(it->entries[0].data);
        printf("RxPDO: Found %u mapping entries\n", num_entries);

        for (uint8_t i = 1; i <= num_entries && i < it->entries.size(); ++i)
        {
            uint32_t mapping_value = *reinterpret_cast<uint32_t *>(it->entries[i].data);
            PdoMappingEntry entry = PdoMappingEntry::parse(mapping_value);

            printf("  Entry %u: 0x%04X:%02X (%u bits)\n",
                   i, entry.index, entry.subindex, entry.bit_length);

            rxpdo_entries_.push_back(entry);
            rxpdo_size_ += (entry.bit_length + 7) / 8;
        }

        printf("RxPDO total size: %zu bytes\n", rxpdo_size_);
        return true;
    }

    // Pack sensor data into TxPDO buffer based on current mapping
    void packTxPdo(const SensorData &sensor, uint8_t *buffer)
    {
        size_t offset = 0;

        for (const auto &entry : txpdo_entries_)
        {
            int16_t value = 0;

            // Map object index to sensor field
            switch (entry.index)
            {
            case 0x6000:
                value = sensor.accel_x;
                break;
            case 0x6001:
                value = sensor.accel_y;
                break;
            case 0x6002:
                value = sensor.accel_z;
                break;
            case 0x6003:
                value = sensor.mag_x;
                break;
            case 0x6004:
                value = sensor.mag_y;
                break;
            case 0x6005:
                value = sensor.mag_z;
                break;
            default:
                printf("WARNING: Unknown TxPDO index 0x%04X\n", entry.index);
                break;
            }

            // Write value to buffer (little-endian)
            if (entry.bit_length == 16)
            {
                buffer[offset++] = value & 0xFF;
                buffer[offset++] = (value >> 8) & 0xFF;
            }
            else if (entry.bit_length == 8)
            {
                buffer[offset++] = value & 0xFF;
            }
        }
    }

    // Unpack RxPDO buffer into LED data based on current mapping
    void unpackRxPdo(const uint8_t *buffer, LedData &leds)
    {
        size_t offset = 0;

        for (const auto &entry : rxpdo_entries_)
        {
            uint8_t value = 0;

            if (entry.bit_length == 8)
            {
                value = buffer[offset++];
            }
            else if (entry.bit_length == 16)
            {
                value = buffer[offset++];
                offset++; // Skip upper byte for 16-bit reads
            }

            // Map object index to LED field
            switch (entry.index)
            {
            case 0x7000:
                leds.led_r = value;
                break;
            case 0x7001:
                leds.led_g = value;
                break;
            case 0x7002:
                leds.led_b = value;
                break;
            default:
                printf("WARNING: Unknown RxPDO index 0x%04X\n", entry.index);
                break;
            }
        }
    }

    size_t getTxPdoSize() const { return txpdo_size_; }
    size_t getRxPdoSize() const { return rxpdo_size_; }

private:
    std::vector<PdoMappingEntry> txpdo_entries_;
    std::vector<PdoMappingEntry> rxpdo_entries_;
    size_t txpdo_size_;
    size_t rxpdo_size_;
};

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    std::shared_ptr<SPI> spi_driver = std::make_shared<SPI>();
    spi_driver->open("/dev/spi0", 0, 0, 10000000);

    Lan9252 esc = Lan9252(spi_driver);
    int32_t rc = esc.init();
    if (rc < 0)
    {
        printf("error init %ld - %s\n", rc, strerror(-rc));
    }

    PDO pdo(&esc);
    slave::Slave slave(&esc, &pdo);

    constexpr uint32_t pdo_size = 16;

    uint8_t buffer_in[pdo_size];
    uint8_t buffer_out[pdo_size];

    // Create mailbox and Object Dictionary
    mailbox::response::Mailbox mbx(&esc, 1024);
    auto dictionary = CoE::createOD();

    // Dynamic PDO manager
    DynamicPdoManager pdo_manager;
    bool pdo_configured = false;

    // Parse to get initial mapping (will be updated later)
    pdo_manager.parseTxPdoMapping(dictionary);
    pdo_manager.parseRxPdoMapping(dictionary);

    mbx.enableCoE(std::move(dictionary));
    slave.setMailbox(&mbx);

    // Set initial PDO buffers (will be resized after mapping is configured)
    pdo.setInput(buffer_in);
    pdo.setOutput(buffer_out);

    uint8_t esc_config;
    esc.read(reg::ESC_CONFIG, &esc_config, sizeof(esc_config));
    bool is_emulated = esc_config & PDI_EMULATION;
    printf("esc config 0x%x, is emulated %i \n", esc_config, is_emulated);

    uint8_t pdi_config;
    esc.read(reg::PDI_CONFIGURATION, &pdi_config, sizeof(pdi_config));
    printf("pdi config 0x%x \n", pdi_config);

    slave.start();

    // Init sensor
    int sensor_fd = open("/dev/accel0", O_RDONLY);
    if (sensor_fd < 0)
    {
        printf("Failed to open sensor device\n");
        return -1;
    }
    fxos8700cq_data sensor_data;

    // Init userleds
    int led_fd = open("/dev/userleds", O_WRONLY);
    if (led_fd < 0)
    {
        printf("Failed to open LED driver\n");
        return -1;
    }

    constexpr uint8_t LED_R_BIT = 1 << 0;
    constexpr uint8_t LED_G_BIT = 1 << 1;
    constexpr uint8_t LED_B_BIT = 1 << 2;

    // In-memory sensor and LED data
    SensorData sensor_values = {0};
    LedData led_values = {0};

    // Print buffer_in and buffer_out addresses
    printf("buffer_in address: %p\n", static_cast<void *>(buffer_in));
    printf("buffer_out address: %p\n", static_cast<void *>(buffer_out));

    while (true)
    {
        slave.routine();

        // When transitioning to SAFE_OP, parse the PDO configuration
        if (slave.state() == State::SAFE_OP && !pdo_configured)
        {
            printf("\n=== Parsing PDO Configuration ===\n");

            // Get the actual OD that reflects master's writes
            auto &od = mbx.getDictionary();

            if (pdo_manager.parseTxPdoMapping(od) && pdo_manager.parseRxPdoMapping(od))
            {
                printf("=== PDO Configuration Successful ===\n");
                printf("TxPDO size: %zu bytes\n", pdo_manager.getTxPdoSize());
                printf("RxPDO size: %zu bytes\n", pdo_manager.getRxPdoSize());

                // Print buffer_in and buffer_out addresses
                printf("buffer_in address: %p\n", static_cast<void *>(buffer_in));
                printf("buffer_out address: %p\n", static_cast<void *>(buffer_out));

                pdo_configured = true;
            }
            else
            {
                printf("ERROR: Failed to parse PDO configuration\n");
            }
        }

        if (slave.state() == State::SAFE_OP && pdo_configured)
        {
            if (buffer_out[0] != 0xFF)
            {
                slave.validateOutputData();
            }
        }

        // Read sensor data
        if (read(sensor_fd, &sensor_data, sizeof(sensor_data)) == sizeof(sensor_data))
        {
            sensor_values.accel_x = sensor_data.accel.x;
            sensor_values.accel_y = sensor_data.accel.y;
            sensor_values.accel_z = sensor_data.accel.z;
            sensor_values.mag_x = sensor_data.magn.x;
            sensor_values.mag_y = sensor_data.magn.y;
            sensor_values.mag_z = sensor_data.magn.z;
        }

        // Pack sensor data into TxPDO buffer
        pdo_manager.packTxPdo(sensor_values, buffer_in);

        // Unpack RxPDO buffer into LED data
        pdo_manager.unpackRxPdo(buffer_out, led_values);

        // Update LEDs based on output PDO
        userled_set_t led_set = 0;
        if (buffer_out[0])
            led_set |= LED_R_BIT;
        if (buffer_out[1])
            led_set |= LED_G_BIT;
        if (buffer_out[2])
            led_set |= LED_B_BIT;

        int ret = ioctl(led_fd, ULEDIOC_SETALL, led_set);
        if (ret < 0)
        {
            int errcode = errno;
            printf("ERROR: ioctl(ULEDIOC_SETALL) failed: %d\n", errcode);
        }
    }

    close(sensor_fd);
    close(led_fd);

    return 0;
}
