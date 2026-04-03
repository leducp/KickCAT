#ifndef KICKMSG_OS_SHARED_MEMORY_H
#define KICKMSG_OS_SHARED_MEMORY_H

#include <cstddef>
#include <string>

#include "types.h"

namespace kickmsg
{

    /// \brief RAII wrapper around a named shared-memory region.
    ///
    /// Platform-specific implementations live in src/OS/{Unix,Windows}/.
    class SharedMemory
    {
    public:
        SharedMemory() = default;
        ~SharedMemory();

        SharedMemory(SharedMemory const&) = delete;
        SharedMemory& operator=(SharedMemory const&) = delete;

        SharedMemory(SharedMemory&& other) noexcept;
        SharedMemory& operator=(SharedMemory&& other) noexcept;

        /// Create a new shared-memory region.
        /// Truncates to \p size bytes and maps it read/write.
        /// Throws if the region already exists.
        void create(std::string const& name, std::size_t size);

        /// Attempt to create a new shared-memory region.
        /// Returns true if the region was created, false if it already exists.
        /// Does NOT throw on "already exists".
        bool try_create(std::string const& name, std::size_t size);

        /// Open an existing shared-memory region (read/write).
        /// Discovers the size from the OS and maps it.
        /// Throws if the region does not exist.
        void open(std::string const& name);

        /// Unmap and release the handle. Called automatically by the destructor.
        void close();

        /// Remove the shared-memory object from the filesystem.
        static void unlink(std::string const& name);

        void*       address() const { return address_; }
        std::size_t size()    const { return size_; }
        bool        is_open() const { return address_ != nullptr; }

    private:
        std::size_t size_{0};
        void*       address_{nullptr};
        os_handle   handle_{INVALID_OS_HANDLE};
    };

} // namespace kickmsg

#endif // KICKMSG_OS_SHARED_MEMORY_H
