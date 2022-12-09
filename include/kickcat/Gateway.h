#ifndef KICKCAT_GATEWAY_H
#define KICKCAT_GATEWAY_H

#include "AbstractDiagSocket.h"
#include "Bus.h"
#include "Mailbox.h"

namespace kickcat
{
    /// \brief Handle external mailbox requests as defined in ETG.8200
    class Gateway
    {
    public:
        /// \param socket       Socket to communicate with external networks
        /// \param addMessage   Callback that enable the gateway instance to transfer the request to the EtherCAT bus
        Gateway(std::shared_ptr<AbstractDiagSocket> socket, std::function<std::shared_ptr<GatewayMessage>(uint8_t const*, int32_t, uint16_t)> addMessage)
            : socket_{socket}
            , addMessage_{addMessage}
            , pendingRequests_{}
        { }
        virtual ~Gateway() = default;

        /// \brief   Fetch a request on the external network and process it if any
        /// \details Try to get a request from the network (non blocking call). If there is data, check the request coherency then
        ///          transfer it to the EtherCAT bus (through addMessage callback) while adding it to the pending requests
        void fetchRequest();

        /// \brief   Process pending requests
        /// \details A pending request is a request that was successfully transfered to the EtherCAT bus, waiting for its completion.
        ///          All requests completed on the bus are sent back to their respective requestors
        void processPendingRequests();

    private:
        std::shared_ptr<AbstractDiagSocket> socket_;
        std::function<std::shared_ptr<GatewayMessage>(uint8_t const*, int32_t, uint16_t)> addMessage_;
        std::vector<std::shared_ptr<GatewayMessage>> pendingRequests_;
    };
}

#endif
