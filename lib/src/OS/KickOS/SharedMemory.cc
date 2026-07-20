// KickOS SharedMemory backend: not implemented yet. open() throws so the full library links
// for KickOS while the real segment mapping is written. unlink() is a no-op (nothing to remove).
#include "OS/SharedMemory.h"
#include "Error.h"

// open() is a throw-only placeholder: -Wmissing-noreturn is expected until the real backend lands.
#pragma GCC diagnostic ignored "-Wmissing-noreturn"

namespace kickcat
{
    SharedMemory::SharedMemory() = default;

    SharedMemory::~SharedMemory() = default;

    void SharedMemory::open(std::string const&, std::size_t, void*)
    {
        THROW_ERROR("SharedMemory::open() not implemented on KickOS");
    }

    void SharedMemory::unlink(std::string const&)
    {
    }
}
