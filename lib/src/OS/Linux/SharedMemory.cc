#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "Error.h"
#include "OS/SharedMemory.h"

namespace kickcat
{
    SharedMemory::SharedMemory()
        : fd_{-1}
    {

    }

    SharedMemory::~SharedMemory()
    {
        if (address_ != nullptr)
        {
            // Remove the mapped memory segment from the address space of the process.
            int rc = munmap(address_, size_);
            if (rc < 0)
            {
                THROW_SYSTEM_ERROR("munmap()");
            }
        }

        if (fd_ >= 0)
        {
            int rc = close(fd_);
            if (rc < 0)
            {
                THROW_SYSTEM_ERROR("close()");
            }
        }
    }

    void SharedMemory::open(std::string const& name, std::size_t size, void* address)
    {
        // Open the shared memory (R/W), create it if needed.
        fd_ = shm_open(name.c_str(), O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
        if (fd_ < 0)
        {
            THROW_SYSTEM_ERROR("shm_open()");
        }

        // Adapt the size
        size_ = size;
        int rc = ftruncate(fd_, size_);
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("ftruncate()");
        }

        // Map the memory segment in our address space (R/W)
        address_ = mmap(address, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (MAP_FAILED == address_)
        {
            address_ = nullptr;
            THROW_SYSTEM_ERROR("mmap()");
        }
    }
}
