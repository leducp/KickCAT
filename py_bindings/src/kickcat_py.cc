#include <nanobind/nanobind.h>

namespace nb = nanobind;

namespace kickcat
{
    void create_python_bindings(nb::module_ &m);

    NB_MODULE(kickcat, m)
    {
        m.doc() = "KickCAT EtherCAT bindings";

        create_python_bindings(m);
    }
}
