#include <nanobind/nanobind.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/vector.h>

#include "kickcat/protocol.h"
#include "kickcat/CoE/mailbox/request.h"

namespace nb = nanobind;
using namespace nb::literals;

namespace kickcat
{
    struct SDOMessagePy
    {
        void set_address(uint16_t addr)
        {
            if (msg == nullptr)
            {
                throw std::runtime_error("Message not initialized");
            }
            msg->setAddress(addr);
        }
        uint16_t address() const
        {
            if (msg == nullptr)
            {
                throw std::runtime_error("Message not initialized");
            }
            return msg->address();
        }
        uint32_t status()
        {
            if (msg == nullptr)
            {
                throw std::runtime_error("Message not initialized");
            }
            return msg->status();
        }

        std::shared_ptr<mailbox::request::AbstractMessage> msg{};
        std::vector<uint8_t> data{};
        uint32_t size;
    };

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

        nb::class_<SDOMessagePy>(m_request, "SDOMessage")
            .def(nb::init<>(), "Default constructor")
            .def_ro("data",     &SDOMessagePy::data, "SDO data")
            .def_ro("size",     &SDOMessagePy::size, "SDO data size")
            .def("set_address", &SDOMessagePy::set_address)
            .def("address",     &SDOMessagePy::address)
            .def("status",      &SDOMessagePy::status);


        // Bind the request Mailbox class
        nb::class_<Mailbox>(m_request, "Mailbox")
            .def(nb::new_([](int size)
            {
                auto mbx = new Mailbox();
                mbx->recv_size = size;
                mbx->send_size = size;
                return mbx;
            }))
            .def("read_sdo",
                [](Mailbox& self, uint16_t index, uint8_t subindex, bool CA, nanoseconds timeout)
                {
                    auto msg_py = std::make_shared<SDOMessagePy>();
                    msg_py->data.resize(self.recv_size);
                    msg_py->size = self.recv_size; // no support for segmented transfer yet
                    msg_py->msg = self.createSDO(index, subindex, CA, CoE::SDO::request::UPLOAD, msg_py->data.data(), &msg_py->size, timeout);

                    return msg_py;
                },
                "index"_a, "subindex"_a, "CA"_a = false, "timeout"_a = milliseconds(100),
                "Read an SDO")
            .def("write_sdo",
                [](Mailbox& self, uint16_t index, uint8_t subindex, bool CA, nb::bytes data, nanoseconds timeout)
                {
                    auto msg_py = std::make_shared<SDOMessagePy>();
                    msg_py->data = std::vector<uint8_t>(data.c_str(), data.c_str() + data.size());
                    msg_py->size = static_cast<uint32_t>(msg_py->data.size());
                    msg_py->msg = self.createSDO(index, subindex, CA, CoE::SDO::request::DOWNLOAD, msg_py->data.data(), &msg_py->size, timeout);

                    return msg_py;
                },
                "index"_a, "subindex"_a, "CA"_a = false, "data"_a, "timeout"_a = milliseconds(100),
                "Write an SDO");
    }
}
