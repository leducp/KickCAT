#ifndef KICKCAT_EXAMPLE_FREEDOM_SLAVE_H
#define KICKCAT_EXAMPLE_FREEDOM_SLAVE_H

#include "kickcat/AbstractSPI.h"
#include "kickcat/protocol.h"

#include <cstdint>
#include <memory>

// Shared LAN9252 EtherCAT-slave core for the Freedom-K64F example -- the CTT-proven
// slave sequence, compiled verbatim by the NuttX example and the KickOS consumer.
// Platforms differ only in the SPI backend and the Hooks below.
namespace freedom
{
    // TxPDO inputs 0x6000..0x6005 (slave -> master), RxPDO outputs 0x7000..0x7002
    // (master -> slave). Null until the first SAFE_OP bind.
    struct Pdo
    {
        int16_t* ax;
        int16_t* ay;
        int16_t* az;
        int16_t* mx;
        int16_t* my;
        int16_t* mz;
        uint8_t* led_r;
        uint8_t* led_g;
        uint8_t* led_b;
    };

    struct Hooks
    {
        void* ctx;

        // Emit one already-formatted console line.
        void (*log)(void* ctx, char const* line);

        // Every OPERATIONAL cycle: fill the TxPDO inputs and consume the RxPDO outputs.
        void (*on_operational)(void* ctx, Pdo const& io);

        // Optional (null disables): called each loop iteration with the current state.
        void (*on_cycle)(void* ctx, kickcat::State state);
    };

    // Runs the CTT-proven slave sequence over `spi`. Never returns.
    int run(std::shared_ptr<kickcat::AbstractSPI> spi, Hooks const& hooks);
}

#endif // KICKCAT_EXAMPLE_FREEDOM_SLAVE_H
