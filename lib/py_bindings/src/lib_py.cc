#include <nanobind/nanobind.h>

namespace nb = nanobind;


namespace kickcat
{
    // Submodules factories
    void create_mailbox_python_bindings(nb::module_ &m);

    void create_python_bindings(nb::module_ &m)
    {
        auto m_mailbox = m.def_submodule("mailbox", "EtherCAT mailbox");
        create_mailbox_python_bindings(m_mailbox);
    }
}