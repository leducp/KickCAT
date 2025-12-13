#include <nanobind/nanobind.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/helpers.h"
#include "kickcat/Error.h"

namespace nb = nanobind;
using namespace nb::literals;

namespace kickcat
{
    void create_bus_python_bindings(nb::module_ &m)
    {
        nb::enum_<Bus::Access>(m, "Access")
            .value("PARTIAL", Bus::Access::PARTIAL)
            .value("COMPLETE", Bus::Access::COMPLETE)
            .value("EMULATE_COMPLETE", Bus::Access::EMULATE_COMPLETE)
            .export_values();

        nb::class_<Link>(m, "Link")
            .def("set_timeout", &Link::setTimeout);

        m.def("create_link", [](std::string const& nom, std::string const& red)
            {
                auto [s1, s2] = createSockets(nom, red);
                return std::make_shared<Link>(s1, s2, []()
                {
                    printf("Redundancy activated\n");
                });
            });

        nb::class_<Bus>(m, "Bus")
            .def(nb::init<std::shared_ptr<Link>>())
            .def("init", &Bus::init)
            .def("slaves", &Bus::slaves, nb::rv_policy::reference_internal)
            .def("get_state", &Bus::getCurrentState)
            .def("request_state", &Bus::requestState)
            .def("wait_for_state", [](Bus &self, State state, std::chrono::nanoseconds timeout)
                {
                    auto noop = [](){};
                    self.waitForState(state, timeout, noop);
                })
            .def("wait_for_state", [](Bus &self, State state, std::chrono::nanoseconds timeout, nb::callable callback)
                {
                    auto cpp_callback = [callback]()
                    {
                        callback();  // Call the Python function
                    };
                    self.waitForState(state, timeout, cpp_callback);
                })
            .def("process_data", [](Bus &self)
                {
                    auto read_error  = [](DatagramState const& state)
                    {
                        std::string error_message = "read error: datagram state is ";
                        error_message += toString(state);
                        throw std::runtime_error(error_message);
                    };
                    auto write_error = [](DatagramState const& state)
                    {
                        std::string error_message = "write error: datagram state is ";
                        error_message += toString(state);
                        throw std::runtime_error(error_message);
                    };
                    self.processDataRead(read_error);
                    self.processDataWrite(write_error);
                })
            .def("process_mailboxes", [](Bus &self)
                {
                    auto check_error = [](DatagramState const& state)
                    {
                        std::string error_message = "check mailboxes: datagram state is ";
                        error_message += toString(state);
                        throw std::runtime_error(error_message);
                    };
                    self.checkMailboxes(check_error);

                    auto process_error  = [](DatagramState const& state)
                    {
                        std::string error_message = "process messages: datagram state is ";
                        error_message += toString(state);
                        throw std::runtime_error(error_message);
                    };
                    self.processMessages(process_error);
                })
            .def("send_logical_read", [](Bus &self, nb::callable error_callback)
                {
                    std::function<void(DatagramState const&)> cpp_callback =
                        [error_callback](DatagramState const& state)
                        {
                            nb::gil_scoped_acquire acquire;
                            error_callback(state);
                        };
                    self.sendLogicalRead(cpp_callback);
                })
            .def("send_logical_write", [](Bus &self, nb::callable error_callback)
                {
                    std::function<void(DatagramState const&)> cpp_callback =
                        [error_callback](DatagramState const& state)
                        {
                            nb::gil_scoped_acquire acquire;
                            error_callback(state);
                        };
                    self.sendLogicalWrite(cpp_callback);
                })
            .def("send_refresh_error_counters", [](Bus &self, nb::callable error_callback)
                {
                    std::function<void(DatagramState const&)> cpp_callback =
                        [error_callback](DatagramState const& state)
                        {
                            nb::gil_scoped_acquire acquire;
                            error_callback(state);
                        };
                    self.sendRefreshErrorCounters(cpp_callback);
                })
            .def("send_mailboxes_read_checks", [](Bus &self, nb::callable error_callback)
                {
                    std::function<void(DatagramState const&)> cpp_callback =
                        [error_callback](DatagramState const& state)
                        {
                            nb::gil_scoped_acquire acquire;
                            error_callback(state);
                        };
                    self.sendMailboxesReadChecks(cpp_callback);
                })
            .def("send_mailboxes_write_checks", [](Bus &self, nb::callable error_callback)
                {
                    std::function<void(DatagramState const&)> cpp_callback =
                        [error_callback](DatagramState const& state)
                        {
                            nb::gil_scoped_acquire acquire;
                            error_callback(state);
                        };
                    self.sendMailboxesWriteChecks(cpp_callback);
                })
            .def("send_read_messages", [](Bus &self, nb::callable error_callback)
                {
                    std::function<void(DatagramState const&)> cpp_callback =
                        [error_callback](DatagramState const& state)
                        {
                            nb::gil_scoped_acquire acquire;
                            error_callback(state);
                        };
                    self.sendReadMessages(cpp_callback);
                })
            .def("send_write_messages", [](Bus &self, nb::callable error_callback)
                {
                    std::function<void(DatagramState const&)> cpp_callback =
                        [error_callback](DatagramState const& state)
                        {
                            nb::gil_scoped_acquire acquire;
                            error_callback(state);
                        };
                    self.sendWriteMessages(cpp_callback);
                })
            .def("finalize_datagrams", &Bus::finalizeDatagrams)
            .def("process_awaiting_frames", &Bus::processAwaitingFrames)
            .def("create_mapping", [](Bus &self, int size)
                {
                    static std::vector<uint8_t> io_buffer(size);
                    self.createMapping(io_buffer.data());
                }, "size"_a = 4096)
            .def("read_sdo", [](Bus &self, Slave& slave, uint16_t index, uint8_t subindex,
                               Bus::Access ca, uint32_t max_data_size = 4,
                               std::chrono::nanoseconds timeout = std::chrono::seconds(1))
                {
                    std::vector<uint8_t> buffer(max_data_size);
                    uint32_t actual_size = max_data_size;

                    self.readSDO(slave, index, subindex, ca, buffer.data(), &actual_size, timeout);

                    // Resize to actual data size and return as bytes
                    buffer.resize(actual_size);
                    return nb::bytes(reinterpret_cast<const char*>(buffer.data()), actual_size);
                })
            .def("read_object_description", [](Bus &self, Slave& slave, uint16_t index) -> std::tuple<std::string, std::string>
                {
                    char buffer[4096];
                    uint32_t buffer_size = 4096; // in bytes

                    auto sdo = slave.mailbox.createSDOInfoGetOD(index, &buffer, &buffer_size, 100ms);
                    self.waitForMessage(sdo);
                    if (sdo->status() != mailbox::request::MessageStatus::SUCCESS)
                    {
                        THROW_ERROR_CODE("Error while get Object Description", error::category::CoE, sdo->status());
                    }

                    CoE::SDO::information::ObjectDescription* description = reinterpret_cast<CoE::SDO::information::ObjectDescription*>(buffer);
                    std::string name{buffer + sizeof(CoE::SDO::information::ObjectDescription), buffer_size - sizeof(CoE::SDO::information::ObjectDescription)};

                    return {name, toString(*description)};
                })
            .def("read_entry_description",  [](Bus &self, Slave& slave, uint16_t index, uint8_t subindex) -> std::tuple<std::string, std::string>
                {
                    char buffer[4096];
                    uint32_t buffer_size = 4096; // in bytes

                    auto sdo = slave.mailbox.createSDOInfoGetED(index, subindex, CoE::SDO::information::ValueInfo::DEFAULT, &buffer, &buffer_size, 100ms);
                    self.waitForMessage(sdo);
                    if (sdo->status() != mailbox::request::MessageStatus::SUCCESS)
                    {
                        THROW_ERROR_CODE("Error while get Entry Description", error::category::CoE, sdo->status());
                    }

                    CoE::SDO::information::EntryDescription* description = reinterpret_cast<CoE::SDO::information::EntryDescription*>(buffer);
                    std::string name{buffer + sizeof(CoE::SDO::information::EntryDescription), buffer_size - sizeof(CoE::SDO::information::EntryDescription)};

                    return {name, toString(*description)};
                });
    }
}
