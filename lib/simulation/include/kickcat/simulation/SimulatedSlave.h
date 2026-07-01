#ifndef KICKCAT_SIMULATION_SIMULATED_SLAVE_H
#define KICKCAT_SIMULATION_SIMULATED_SLAVE_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "kickcat/CoE/OD.h"
#include "kickcat/CoE/mailbox/response.h"
#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/PDO.h"
#include "kickcat/simulation/DeviceApp.h"
#include "kickcat/slave/Slave.h"

namespace kickcat::sim
{
    namespace fs = std::filesystem;

    // One emulated slave. unique_ptr members (PDO/Slave/Mailbox hold raw pointers
    // into the ESC) and vectors (whose data() survives a move) make the aggregate
    // safe to hold in a std::vector.
    struct SimulatedSlave
    {
        std::unique_ptr<EmulatedESC>                          esc;
        std::unique_ptr<PDO>                                  pdo;
        std::unique_ptr<slave::Slave>                         slave;
        std::unique_ptr<CoE::Dictionary>                      dictionary;  // ESI/coe_xml OD, if any
        std::unique_ptr<mailbox::response::Mailbox>           mailbox;     // only if CoE is advertised
        std::unique_ptr<DeviceApp>                            device;      // behaviour (e.g. DS402), if any
        std::vector<uint8_t>                                  input;
        std::vector<uint8_t>                                  output;
    };

    // Process-image buffer; full-frame sized (updateInput/updateOutput copy the SM length).
    constexpr uint32_t PDO_MAX_SIZE = 4096;

    // Build one slave from its JSON config (ESI device or raw eeprom, optional CoE).
    // Throws std::runtime_error on any failure.
    SimulatedSlave buildSlave(fs::path const& config_path);
}

#endif
