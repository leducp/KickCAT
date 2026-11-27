#ifndef KICKCAT_HELPERS_H
#define KICKCAT_HELPERS_H

#include <memory>
#include <tuple>

#include "kickcat/AbstractSocket.h"
#include "kickcat/OS/Time.h"

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

    class Bus;

    /// \brief Scan the bus until it shows the expected number of slaves, for slaves that boot later than the master
    /// \param bus      The bus to scan, before its init()
    /// \param expected Number of slaves to wait for
    /// \param timeout  Maximum time to wait
    /// \param period   Time between two scans
    /// \return The number of slaves of the last scan: expected on success, what the bus showed on timeout
    int32_t waitForSlaves(Bus& bus, int32_t expected, nanoseconds timeout, nanoseconds period = 100ms);
}

#endif
