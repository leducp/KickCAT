#include <nanobind/nanobind.h>

#include "kickcat/Error.h"
#include "kickcat/protocol.h"

namespace nb = nanobind;

namespace kickcat
{
    // Factories
    void create_mailbox_python_bindings(nb::module_ &m);
    void create_bus_python_bindings(nb::module_ &m);
    void create_slave_python_bindings(nb::module_ &m);

    void create_python_bindings(nb::module_ &m)
    {
        nb::exception<ErrorCode>(m, "KickcatError");

        nb::enum_<State>(m, "State")
            .value("INIT",        State::INIT)
            .value("PREOP",       State::PRE_OP)
            .value("SAFE_OP",     State::SAFE_OP)
            .value("OPERATIONAL", State::OPERATIONAL);

        auto m_mailbox = m.def_submodule("mailbox", "EtherCAT mailbox");
        create_mailbox_python_bindings(m_mailbox);

        create_slave_python_bindings(m);
        create_bus_python_bindings(m);
    }
}
