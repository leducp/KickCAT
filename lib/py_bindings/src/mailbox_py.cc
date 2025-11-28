#include <nanobind/nanobind.h>
#include <nanobind/trampoline.h>

#include "kickcat/protocol.h"
#include "kickcat/CoE/mailbox/request.h"

namespace nb = nanobind;
using namespace nb::literals;

namespace kickcat
{
    void create_mailbox_python_bindings(nb::module_ &m)
    {
        using namespace mailbox::request;

        auto m_request = m.def_submodule("request", "request");

        // Bind MessageStatus constants
        auto message_status = m_request.def_submodule("MessageStatus", "Message status constants");
        message_status.attr("SUCCESS") = MessageStatus::SUCCESS;
        message_status.attr("RUNNING") = MessageStatus::RUNNING;
        message_status.attr("TIMEDOUT") = MessageStatus::TIMEDOUT;
        message_status.attr("COE_WRONG_SERVICE") = MessageStatus::COE_WRONG_SERVICE;
        message_status.attr("COE_UNKNOWN_SERVICE") = MessageStatus::COE_UNKNOWN_SERVICE;
        message_status.attr("COE_CLIENT_BUFFER_TOO_SMALL") = MessageStatus::COE_CLIENT_BUFFER_TOO_SMALL;
        message_status.attr("COE_SEGMENT_BAD_TOGGLE_BIT") = MessageStatus::COE_SEGMENT_BAD_TOGGLE_BIT;

        // Bind the request Mailbox class
        nb::class_<Mailbox>(m_request, "Mailbox")
            .def(nb::init<>(), "Default constructor")

            // Public member variables
            .def_rw("recv_offset", &Mailbox::recv_offset,
                    "Receive mailbox offset")
            .def_rw("recv_size", &Mailbox::recv_size,
                    "Receive mailbox size")
            .def_rw("send_offset", &Mailbox::send_offset,
                    "Send mailbox offset")
            .def_rw("send_size", &Mailbox::send_size,
                    "Send mailbox size")
            .def_rw("can_read", &Mailbox::can_read,
                    "Data available on the slave")
            .def_rw("can_write", &Mailbox::can_write,
                    "Free space for a new message on the slave")
            .def_rw("counter", &Mailbox::counter,
                    "Session handle, from 1 to 7")
            .def_rw("toggle", &Mailbox::toggle,
                    "For SDO segmented transfer")
            .def_rw("emergencies", &Mailbox::emergencies,
                    "List of CoE emergencies");
    }
}
