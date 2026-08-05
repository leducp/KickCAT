// KickOS filesystem backend: not implemented yet. Everything throws so the full library links for
// KickOS; nothing on that target reads files today (EmulatedESC only needs it for the host-side
// EEPROM loaders).
#include "Error.h"
#include "OS/Filesystem.h"

// Throw-only placeholders: -Wmissing-noreturn is expected until a real backend lands.
#pragma GCC diagnostic ignored "-Wmissing-noreturn"

namespace kickcat::filesystem
{
    bool exists(std::string const&)
    {
        THROW_ERROR("filesystem::exists() not implemented on KickOS");
    }

    bool isDirectory(std::string const&)
    {
        THROW_ERROR("filesystem::isDirectory() not implemented on KickOS");
    }

    bool createDirectory(std::string const&)
    {
        THROW_ERROR("filesystem::createDirectory() not implemented on KickOS");
    }

    bool removeFile(std::string const&)
    {
        THROW_ERROR("filesystem::removeFile() not implemented on KickOS");
    }

    bool removeDirectory(std::string const&)
    {
        THROW_ERROR("filesystem::removeDirectory() not implemented on KickOS");
    }

    std::vector<Entry> list(std::string const&)
    {
        THROW_ERROR("filesystem::list() not implemented on KickOS");
    }

    std::vector<uint8_t> readFile(std::string const&)
    {
        THROW_ERROR("filesystem::readFile() not implemented on KickOS");
    }

    void writeFile(std::string const&, void const*, std::size_t)
    {
        THROW_ERROR("filesystem::writeFile() not implemented on KickOS");
    }
}
