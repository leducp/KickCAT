#include <string>

#include <nanobind/nanobind.h>

#include "kickcat/Error.h"
#include "kickcat/FoE/protocol.h"
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
        nb::exception<ErrorAL>(m,  "ErrorAL");
        nb::exception<ErrorCoE>(m, "ErrorCoE");

        static nb::exception<ErrorFoE> foe_error(m, "ErrorFoE");
        nb::register_exception_translator([](std::exception_ptr const& p, void* payload)
        {
            try
            {
                std::rethrow_exception(p);
            }
            catch (ErrorFoE const& e)
            {
                std::string message = std::string{e.what()} + ": " + FoE::errorToString(static_cast<uint32_t>(e.code()));
                PyErr_SetString(static_cast<PyObject*>(payload), message.c_str());
            }
        }, foe_error.ptr());

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
