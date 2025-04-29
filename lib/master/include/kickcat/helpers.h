#ifndef KICKCAT_HELPERS_H
#define KICKCAT_HELPERS_H

#include <memory>
#include <tuple>

#include "kickcat/AbstractSocket.h"

namespace kickcat
{
    /// \brief Allow a user to select an available network interface
    /// \param nominal_if       If '?', a list of interfaces will be displayed to user to choose on
    /// \param redundant_if     If '?', a list of interfaces will be displayed to user to choose on
    void selectInterface(std::string& nominal_if, std::string& redundant_if);

    /// \brief Create a socket. It rely on selectInterface under the hood to simplify the user life
    ///        If a socket name is 'tap', a KickCAT TapSocket is created with "_nominal" or "_redundant" suffix
    /// \param nominal_if   Name of the network interface to use. If '?', the user will choose between the system interfaces lists
    /// \param redundant_if Name of the network interface to use. If '?', the user will choose between the system interfaces lists
    /// \return A created socket
    std::tuple<std::shared_ptr<AbstractSocket>, std::shared_ptr<AbstractSocket>> createSockets(std::string nominal_if, std::string redundant_if);

}

#endif
