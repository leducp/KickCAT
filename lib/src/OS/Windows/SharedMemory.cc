#include <windows.h>
#include <string>
#include <stdexcept>

#include "Error.h"
#include "OS/SharedMemory.h"

namespace kickcat
{
    #define THROW_LAST_ERROR(msg) (throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), LOCATION(": " msg)))

    SharedMemory::SharedMemory(SharedMemory&& other) noexcept
        : size_{other.size_}
        , address_{other.address_}
        , fd_{other.fd_}
    {
        other.size_    = 0;
        other.address_ = nullptr;
        other.fd_      = nullptr;
    }

    SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept
    {
        if (this != &other)
        {
            close();
            size_    = other.size_;
            address_ = other.address_;
            fd_      = other.fd_;
            other.size_    = 0;
            other.address_ = nullptr;
            other.fd_      = nullptr;
        }
        return *this;
    }

    SharedMemory::~SharedMemory()
    {
        close();
    }

    void SharedMemory::map(std::size_t size, void* address)
    {
        address_ = MapViewOfFileEx(fd_, FILE_MAP_ALL_ACCESS, 0, 0, size, address);
        if (address_ == nullptr)
        {
            CloseHandle(fd_);
            fd_ = nullptr;
            THROW_LAST_ERROR("MapViewOfFileEx() failed");
        }
        size_ = size;
    }

    static HANDLE create_file_mapping(std::string const& name, std::size_t size)
    {
        return CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            static_cast<DWORD>((size >> 32) & 0xFFFFFFFF),
            static_cast<DWORD>(size & 0xFFFFFFFF),
            name.c_str());
    }

    void SharedMemory::open(std::string const& name, std::size_t size, void* address)
    {
        fd_ = create_file_mapping(name, size);
        if (fd_ == nullptr)
        {
            THROW_LAST_ERROR("CreateFileMappingA() failed");
        }
        map(size, address);
    }

    void SharedMemory::create(std::string const& name, std::size_t size)
    {
        fd_ = create_file_mapping(name, size);
        if (fd_ == nullptr)
        {
            THROW_LAST_ERROR("CreateFileMappingA(create) failed");
        }
        map(size);
    }

    bool SharedMemory::try_create(std::string const& name, std::size_t size)
    {
        fd_ = create_file_mapping(name, size);
        if (fd_ == nullptr)
        {
            THROW_LAST_ERROR("CreateFileMappingA(try_create) failed");
        }

        // If the mapping already existed, GetLastError() returns ERROR_ALREADY_EXISTS
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(fd_);
            fd_ = nullptr;
            return false;
        }

        map(size);
        return true;
    }

    void SharedMemory::open(std::string const& name)
    {
        fd_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (fd_ == nullptr)
        {
            THROW_LAST_ERROR("OpenFileMappingA() failed");
        }

        // Windows file mappings don't expose their size directly.
        // Map the full region and query the actual size via VirtualQuery.
        address_ = MapViewOfFile(fd_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (address_ == nullptr)
        {
            CloseHandle(fd_);
            fd_ = nullptr;
            THROW_LAST_ERROR("MapViewOfFile() failed");
        }

        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(address_, &info, sizeof(info)) == 0)
        {
            UnmapViewOfFile(address_);
            address_ = nullptr;
            CloseHandle(fd_);
            fd_ = nullptr;
            THROW_LAST_ERROR("VirtualQuery() failed");
        }
        size_ = info.RegionSize;
    }

    void SharedMemory::close()
    {
        if (address_ != nullptr)
        {
            UnmapViewOfFile(address_);
            address_ = nullptr;
        }
        if (fd_ != nullptr)
        {
            CloseHandle(fd_);
            fd_ = nullptr;
        }
        size_ = 0;
    }

    void SharedMemory::unlink(std::string const&)
    {
        // Windows named file mappings are reference-counted by the kernel.
        // They are automatically destroyed when all handles are closed.
        // No explicit unlink needed.
    }
}
