#include "kickmsg/OS/SharedMemory.h"

#include <cerrno>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kickmsg
{

    SharedMemory::~SharedMemory()
    {
        close();
    }

    SharedMemory::SharedMemory(SharedMemory&& other) noexcept
        : size_{other.size_}
        , address_{other.address_}
        , handle_{other.handle_}
    {
        other.size_    = 0;
        other.address_ = nullptr;
        other.handle_  = INVALID_OS_HANDLE;
    }

    SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept
    {
        if (this != &other)
        {
            close();
            size_    = other.size_;
            address_ = other.address_;
            handle_  = other.handle_;
            other.size_    = 0;
            other.address_ = nullptr;
            other.handle_  = INVALID_OS_HANDLE;
        }
        return *this;
    }

    void SharedMemory::create(std::string const& name, std::size_t size)
    {
        handle_ = shm_open(name.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (handle_ < 0)
        {
            throw std::runtime_error("shm_open(create) failed: " + name);
        }

        if (ftruncate(handle_, static_cast<off_t>(size)) < 0)
        {
            ::close(handle_);
            handle_ = INVALID_OS_HANDLE;
            throw std::runtime_error("ftruncate failed: " + name);
        }

        address_ = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, handle_, 0);
        if (address_ == MAP_FAILED)
        {
            address_ = nullptr;
            ::close(handle_);
            handle_ = INVALID_OS_HANDLE;
            throw std::runtime_error("mmap failed: " + name);
        }

        size_ = size;
    }

    bool SharedMemory::try_create(std::string const& name, std::size_t size)
    {
        int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (fd < 0)
        {
            if (errno == EEXIST)
            {
                return false;
            }
            throw std::runtime_error("shm_open(try_create) failed: " + name);
        }

        ::close(fd);
        create(name, size);
        return true;
    }

    void SharedMemory::open(std::string const& name)
    {
        handle_ = shm_open(name.c_str(), O_RDWR, 0);
        if (handle_ < 0)
        {
            throw std::runtime_error("shm_open(open) failed: " + name);
        }

        struct stat st{};
        if (fstat(handle_, &st) < 0)
        {
            ::close(handle_);
            handle_ = INVALID_OS_HANDLE;
            throw std::runtime_error("fstat failed: " + name);
        }

        size_ = static_cast<std::size_t>(st.st_size);
        address_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                         MAP_SHARED, handle_, 0);
        if (address_ == MAP_FAILED)
        {
            address_ = nullptr;
            ::close(handle_);
            handle_ = INVALID_OS_HANDLE;
            throw std::runtime_error("mmap failed: " + name);
        }
    }

    void SharedMemory::close()
    {
        if (address_ != nullptr)
        {
            munmap(address_, size_);
            address_ = nullptr;
        }
        if (handle_ != INVALID_OS_HANDLE)
        {
            ::close(handle_);
            handle_ = INVALID_OS_HANDLE;
        }
        size_ = 0;
    }

    void SharedMemory::unlink(std::string const& name)
    {
        shm_unlink(name.c_str());
    }

} // namespace kickmsg
