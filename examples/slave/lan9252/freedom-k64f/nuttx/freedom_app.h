#ifndef KICKCAT_EXAMPLE_FREEDOM_APP_H
#define KICKCAT_EXAMPLE_FREEDOM_APP_H

#include "freedom_slave.h"

// Shared Freedom-K64F slave application: console sink, state-change trace, Hooks wiring,
// run loop + exception guard. Each platform supplies only its SPI backend and the
// OPERATIONAL data source below.
namespace freedom
{
    // Every OPERATIONAL cycle: fill the TxPDO inputs and consume the RxPDO outputs.
    // data_ctx is handed back unchanged.
    using DataSource = void (*)(void* data_ctx, Pdo const& io);

    // Runs the shared slave app over `spi`; returns nonzero only if the run aborts (exception).
    int app_run(std::shared_ptr<kickcat::AbstractSPI> spi, DataSource data_source, void* data_ctx);
}

#endif // KICKCAT_EXAMPLE_FREEDOM_APP_H
