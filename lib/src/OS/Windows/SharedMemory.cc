#include <windows.h>
#include <string>
#include <stdexcept>

#include "Error.h"
#include "OS/SharedMemory.h"

namespace kickcat
{
    #define  THROW_LAST_ERROR(msg) (throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),  LOCATION(": " msg)))

    SharedMemory::SharedMemory()
        : fd_{nullptr}
    {
    }

    SharedMemory::~SharedMemory()
    {
        if (address_ != nullptr)
        {
            if (not UnmapViewOfFile(address_))
            {
                THROW_LAST_ERROR("UnmapViewOfFile() failed");
            }
        }

        if (fd_ != nullptr)
        {
            if (not CloseHandle(fd_))
            {
                THROW_LAST_ERROR("CloseHandle() failed");
            }
        }
    }

    void SharedMemory::open(std::string const& name, std::size_t size, void* address)
    {
        size_ = size;

        // Create or open shared memory (file mapping in system paging file)
        fd_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE,  // use system paging file
            nullptr,               // default security
            PAGE_READWRITE,        // read/write access
            static_cast<DWORD>((size_ >> 32) & 0xFFFFFFFF),  // high-order size
            static_cast<DWORD>(size_ & 0xFFFFFFFF),          // low-order size
            name.c_str());         // name of the mapping object

        if (fd_ == nullptr)
        {
            THROW_LAST_ERROR("CreateFileMapping() failed");
        }

        // Map the memory view into this process's address space
        address_ = MapViewOfFileEx(
            fd_,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            size_,
            address);   // hint address (can be nullptr for automatic)

        if (address_ == nullptr)
        {
            CloseHandle(fd_);
            fd_ = nullptr;
            THROW_LAST_ERROR("MapViewOfFileEx() failed");
        }
    }
}
