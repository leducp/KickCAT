#ifndef KICKCAT_MASTER_MAILBOX
#define KICKCAT_MASTER_MAILBOX

#include "Mailbox.h"
#include "protocol.h"


namespace kickcat
{

    ///< Brief
    class MasterMailbox
    {
    public:
        MasterMailbox(CoE::MasterDeviceDescription& master_description);
        ~MasterMailbox() = default;

        std::shared_ptr<GatewayMessage> createGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index);

        uint16_t recv_size{128};

    private:
        std::vector<uint8_t> handleSDO(GatewayMessage const& msg);

        CoE::MasterDeviceDescription& master_description_;
    };
}



#endif
