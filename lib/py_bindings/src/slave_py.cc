#include <nanobind/nanobind.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/helpers.h"
#include "kickcat/Error.h"

namespace nb = nanobind;
using namespace nb::literals;

namespace kickcat
{
    void create_slave_python_bindings(nb::module_ &m)
    {
        nb::class_<Slave>(m, "Slave")
            .def_ro("address", &Slave::address)
            .def_prop_ro("input_size", [](Slave const& s) { return s.input.bsize; })
            .def_prop_ro("output_size", [](Slave const& s){ return s.output.bsize; })
            .def_prop_ro("input_data", [](Slave& s)
            {
                return nb::bytes((char *)s.input.data, s.input.bsize);
            })
            .def("set_output_bytes", [](Slave &s, nb::bytes data)
                {
                    size_t n = std::min((size_t)s.output.bsize, data.size());
                    memcpy(s.output.data, data.c_str(), n);
                })
            .def_ro("mailbox", &Slave::mailbox, nb::rv_policy::reference_internal);
    }
}
