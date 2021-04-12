#ifndef KICKCAT_BUS_H
#define KICKCAT_BUS_H

#include <memory>
#include <tuple>

#include "Error.h"
#include "Frame.h"

namespace kickcat
{
    class AbstractSocket;

    class Bus
    {
    public:
        Bus(std::shared_ptr<AbstractSocket> socket);
        ~Bus() = default;

        Error init();
        Error requestState(SM_STATE request);

        uint16_t getSlavesOnNetwork();

    private:
        Error resetSlaves();

        std::shared_ptr<AbstractSocket> socket_;
        Frame frame_;
    };
}

#endif
