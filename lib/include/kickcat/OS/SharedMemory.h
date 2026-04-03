#ifndef KICKCAT_OS_SHARED_MEMORY_H
#define KICKCAT_OS_SHARED_MEMORY_H

#include <cstddef>
#include <string>

#include "kickcat/types.h"

namespace kickcat
{
    /// \brief RAII wrapper around a named shared-memory region.
    ///
    /// Platform-specific implementations live in src/OS/{Unix,Windows}/.
    class SharedMemory
    {
    public:
        SharedMemory() = default;
        SharedMemory(SharedMemory const&) = delete;
        SharedMemory& operator=(SharedMemory const&) = delete;
        SharedMemory(SharedMemory&& other) noexcept;
        SharedMemory& operator=(SharedMemory&& other) noexcept;
        ~SharedMemory();

        /// Create (or open) a shared-memory region and truncate to \p size bytes.
        /// Maps it read/write. Optional \p address hint for mmap (nullptr = auto).
        void open(std::string const& name, std::size_t size, void* address = nullptr);

        /// Create a new shared-memory region. Truncates to \p size bytes.
        /// Throws if the region already exists.
        void create(std::string const& name, std::size_t size);

        /// Attempt to create a new shared-memory region.
        /// Returns true if created, false if it already exists.
        bool try_create(std::string const& name, std::size_t size);

        /// Open an existing shared-memory region (read/write).
        /// Discovers the size from the OS.
        /// Throws if the region does not exist.
        void open(std::string const& name);

        /// Unmap and close the handle. Called automatically by the destructor.
        void close();

        /// Remove the shared-memory object from the filesystem.
        static void unlink(std::string const& name);

        void*       address() const { return address_; }
        std::size_t size()    const { return size_; }
        bool        is_open() const { return address_ != nullptr; }

    private:
        void map(std::size_t size, void* address = nullptr);

        std::size_t size_{0};
        void*       address_{nullptr};
        os_shm      fd_{};
    };
}

#endif
