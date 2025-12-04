#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/function.h>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/helpers.h"
#include "kickcat/Error.h"

namespace nb = nanobind;
using namespace kickcat;

NB_MODULE(kickcat, m)
{
    m.doc() = "High-level Python bindings for KickCAT";

    nb::enum_<State>(m, "State")
        .value("INIT", State::INIT)
        .value("PREOP", State::PRE_OP)
        .value("SAFE_OP", State::SAFE_OP)
        .value("OPERATIONAL", State::OPERATIONAL);

    nb::class_<Slave>(m, "Slave")
        .def_prop_ro("address", [](const Slave &s)
                     { return s.address; })
        .def_prop_ro("input_size", [](const Slave &s)
                     { return s.input.bsize; })
        .def_prop_ro("output_size", [](const Slave &s)
                     { return s.output.bsize; })
        .def_prop_ro("input_data", [](Slave &s)
                     { return nb::bytes((char *)s.input.data, s.input.bsize); })
        .def("set_output_bytes", [](Slave &s, nb::bytes data)
             {
            size_t n = std::min((size_t)s.output.bsize, data.size());
            memcpy(s.output.data, data.c_str(), n); });

    nb::class_<Link>(m, "Link")
        .def("set_timeout", &Link::setTimeout);

    m.def("create_link", [](const std::string &nom, const std::string &red)
          {
        auto [s1, s2] = createSockets(nom, red);
        return std::make_shared<Link>(s1, s2, [](){
            printf("Redundancy activated\n");
        }); });

    nb::class_<Bus>(m, "Bus")
        .def(nb::init<std::shared_ptr<Link>>())
        .def("init", &Bus::init)
        .def("slaves", [](Bus &b)
             { return b.slaves(); }, nb::rv_policy::reference_internal)
        .def("get_state", &Bus::getCurrentState)
        .def("request_state", &Bus::requestState)
        .def("wait_for_state", [](Bus &b, State state, std::chrono::nanoseconds timeout)
             { 
                 auto noop = [](){};
                 b.waitForState(state, timeout, noop); })
        .def("wait_for_state", [](Bus &b, State state, std::chrono::nanoseconds timeout, nb::callable callback)
             { 
                 auto cpp_callback = [callback]() {
                     callback();  // Call the Python function
                 };
                 b.waitForState(state, timeout, cpp_callback); })
        .def("process_data", [](Bus &b)
             {
             auto noop = [](DatagramState const&){};
             b.processDataRead(noop);
             b.processDataWrite(noop); })
        .def("process_frames", &Bus::processAwaitingFrames)
        .def("create_mapping", [](Bus &b)
             {
             static std::vector<uint8_t> io_buffer(2048);
             b.createMapping(io_buffer.data()); });

    nb::exception<ErrorCode>(m, "KickcatError");
}
