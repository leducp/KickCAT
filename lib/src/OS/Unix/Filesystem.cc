#include <cerrno>
#include <string>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Error.h"
#include "OS/Filesystem.h"

namespace kickcat::filesystem
{
    namespace
    {
        constexpr std::size_t CHUNK_SIZE = 64 * 1024;
    }

    bool exists(std::string const& path)
    {
        struct stat info;
        return ::stat(path.c_str(), &info) == 0;
    }

    bool isDirectory(std::string const& path)
    {
        struct stat info;
        if (::stat(path.c_str(), &info) != 0)
        {
            return false;
        }
        return S_ISDIR(info.st_mode);
    }

    bool createDirectory(std::string const& path)
    {
        if (::mkdir(path.c_str(), 0755) == 0)
        {
            return true;
        }
        if (errno == EEXIST)
        {
            return false;
        }
        THROW_SYSTEM_ERROR("mkdir()");
    }

    bool removeFile(std::string const& path)
    {
        if (::unlink(path.c_str()) == 0)
        {
            return true;
        }
        if (errno == ENOENT)
        {
            return false;
        }
        THROW_SYSTEM_ERROR("unlink()");
    }

    bool removeDirectory(std::string const& path)
    {
        if (::rmdir(path.c_str()) == 0)
        {
            return true;
        }
        if (errno == ENOENT)
        {
            return false;
        }
        THROW_SYSTEM_ERROR("rmdir()");
    }

    std::vector<Entry> list(std::string const& path)
    {
        DIR* directory = ::opendir(path.c_str());
        if (directory == nullptr)
        {
            THROW_SYSTEM_ERROR("opendir()");
        }

        std::vector<Entry> entries;
        while (true)
        {
            errno = 0;                      // readdir returns nullptr for both end-of-stream and error
            dirent const* item = ::readdir(directory);
            if (item == nullptr)
            {
                break;
            }

            std::string name{item->d_name};
            if ((name == ".") or (name == ".."))
            {
                continue;
            }

            bool is_directory = (item->d_type == DT_DIR);
            if (item->d_type == DT_UNKNOWN)
            {
                // Not every filesystem fills d_type.
                is_directory = isDirectory(join(path, name));
            }
            entries.push_back(Entry{name, is_directory});
        }

        int code = errno;
        ::closedir(directory);
        if (code != 0)
        {
            THROW_SYSTEM_ERROR_CODE("readdir()", code);
        }
        return entries;
    }

    namespace
    {
        // Takes ownership of fd
        std::vector<uint8_t> readAll(int fd)
        {
            std::vector<uint8_t> content;
            struct stat info;
            if ((::fstat(fd, &info) == 0) and (info.st_size > 0))
            {
                content.reserve(static_cast<std::size_t>(info.st_size));
            }

            uint8_t chunk[CHUNK_SIZE];
            while (true)
            {
                ssize_t count = ::read(fd, chunk, sizeof(chunk));
                if (count == 0)
                {
                    break;
                }
                if (count < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    int code = errno;
                    ::close(fd);
                    THROW_SYSTEM_ERROR_CODE("read()", code);
                }
                content.insert(content.end(), chunk, chunk + count);
            }

            ::close(fd);
            return content;
        }

        // Takes ownership of fd
        void writeAll(int fd, void const* data, std::size_t size)
        {
            auto const* bytes = static_cast<uint8_t const*>(data);
            std::size_t written = 0;
            while (written < size)
            {
                ssize_t count = ::write(fd, bytes + written, size - written);
                if (count < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    int code = errno;
                    ::close(fd);
                    THROW_SYSTEM_ERROR_CODE("write()", code);
                }
                written += static_cast<std::size_t>(count);
            }

            ::close(fd);
        }

        // Closes fd and throws if it is not a regular file with a single name
        void checkRegular(int fd)
        {
            struct stat info;
            if (::fstat(fd, &info) != 0)
            {
                int code = errno;
                ::close(fd);
                THROW_SYSTEM_ERROR_CODE("fstat()", code);
            }
            if (not S_ISREG(info.st_mode))
            {
                ::close(fd);
                THROW_SYSTEM_ERROR_CODE("not a regular file", EINVAL);
            }
            if (info.st_nlink > 1)
            {
                ::close(fd);
                THROW_SYSTEM_ERROR_CODE("file with several hard links", EMLINK);
            }
        }
    }

    std::vector<uint8_t> readFile(std::string const& path)
    {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0)
        {
            THROW_SYSTEM_ERROR("open()");
        }
        return readAll(fd);
    }

    void writeFile(std::string const& path, void const* data, std::size_t size)
    {
        int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
        {
            THROW_SYSTEM_ERROR("open()");
        }
        writeAll(fd, data, size);
    }

    std::vector<uint8_t> readRegularFile(std::string const& path)
    {
        // O_NONBLOCK: opening a FIFO would otherwise wait for a writer before the type check
        int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
        {
            THROW_SYSTEM_ERROR("open()");
        }
        checkRegular(fd);
        return readAll(fd);
    }

    void writeRegularFile(std::string const& path, void const* data, std::size_t size)
    {
        // The replacement takes the permissions of the file it replaces, else those of a new file (umask applied).
        // Only the rwx bits are kept: a set-user-ID file replaced by a privileged process would run network content.
        mode_t mode = 0644;
        struct stat replaced;
        bool const keep_mode = (::lstat(path.c_str(), &replaced) == 0) and S_ISREG(replaced.st_mode);
        if (keep_mode)
        {
            mode = replaced.st_mode & 0777;
        }

        std::string temporary;
        int fd = -1;
        for (int attempt = 0; fd < 0; ++attempt)
        {
            temporary = path + "." + std::to_string(::getpid()) + "." + std::to_string(attempt) + ".tmp";
            fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
            if ((fd < 0) and ((errno != EEXIST) or (attempt >= 100)))
            {
                THROW_SYSTEM_ERROR("open()");
            }
        }
        if (keep_mode and (::fchmod(fd, mode) != 0))    // open() applied the umask
        {
            int code = errno;
            ::close(fd);
            ::unlink(temporary.c_str());
            THROW_SYSTEM_ERROR_CODE("fchmod()", code);
        }

        try
        {
            writeAll(fd, data, size);
        }
        catch (...)
        {
            ::unlink(temporary.c_str());
            throw;
        }

        if (::rename(temporary.c_str(), path.c_str()) != 0)
        {
            int code = errno;
            ::unlink(temporary.c_str());
            THROW_SYSTEM_ERROR_CODE("rename()", code);
        }
    }
}
