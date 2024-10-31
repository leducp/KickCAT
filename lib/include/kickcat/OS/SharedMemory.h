#ifndef KICKAT_LINUX_SHARED_MEMORY_H
#define KICKAT_LINUX_SHARED_MEMORY_H

#include <string>

#include "kickcat/types.h"

namespace kickcat
{
    /// \brief Create if needed, then open and map (read/write) a shared memory segment
    class SharedMemory
    {
    public:
        SharedMemory();
        SharedMemory(SharedMemory const& shm) = delete;
        SharedMemory& operator=(SharedMemory const& shm) = delete;
        ~SharedMemory();

        /// \param  name    Name of the shared memory.
        /// \param  size    Size of the shared memory in bytes.
        /// \param  address Address where the shm segment shall be mapped. Automatic address if nullptr
        void open(std::string const& name, std::size_t size, void* address = nullptr);

        /// \return The address of the shm in this process.
        void* address() { return address_; }

    private:
        std::size_t size_{};    ///< Size in bytes of the shared memory.
        void* address_{};       ///< Address of the shared memory in this processus.
        os_file fd_{};          ///< File descriptor of the shared memory.
    };
}

#endif
