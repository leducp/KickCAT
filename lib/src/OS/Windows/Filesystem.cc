#include <windows.h>

#include <stdexcept>
#include <string>

#include "Error.h"
#include "OS/Filesystem.h"

namespace kickcat::filesystem
{
    #define THROW_LAST_ERROR(msg) (throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), LOCATION(": " msg)))

    namespace
    {
        constexpr DWORD CHUNK_SIZE = 64 * 1024;

        bool isAbsent(DWORD error)
        {
            return (error == ERROR_FILE_NOT_FOUND) or (error == ERROR_PATH_NOT_FOUND);
        }
    }

    bool exists(std::string const& path)
    {
        return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool isDirectory(std::string const& path)
    {
        DWORD attributes = GetFileAttributesA(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool createDirectory(std::string const& path)
    {
        if (CreateDirectoryA(path.c_str(), nullptr) != 0)
        {
            return true;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            return false;
        }
        THROW_LAST_ERROR("CreateDirectory() failed");
    }

    bool removeFile(std::string const& path)
    {
        if (DeleteFileA(path.c_str()) != 0)
        {
            return true;
        }
        if (isAbsent(GetLastError()))
        {
            return false;
        }
        THROW_LAST_ERROR("DeleteFile() failed");
    }

    bool removeDirectory(std::string const& path)
    {
        if (RemoveDirectoryA(path.c_str()) != 0)
        {
            return true;
        }
        if (isAbsent(GetLastError()))
        {
            return false;
        }
        THROW_LAST_ERROR("RemoveDirectory() failed");
    }

    std::vector<Entry> list(std::string const& path)
    {
        WIN32_FIND_DATAA item;
        HANDLE search = FindFirstFileA(join(path, "*").c_str(), &item);
        if (search == INVALID_HANDLE_VALUE)
        {
            THROW_LAST_ERROR("FindFirstFile() failed");
        }

        std::vector<Entry> entries;
        do
        {
            std::string name{item.cFileName};
            if ((name == ".") or (name == ".."))
            {
                continue;
            }
            entries.push_back(Entry{name, (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
        }
        while (FindNextFileA(search, &item) != 0);

        DWORD error = GetLastError();
        FindClose(search);
        if (error != ERROR_NO_MORE_FILES)
        {
            throw std::system_error(static_cast<int>(error), std::system_category(), LOCATION(": FindNextFile() failed"));
        }
        return entries;
    }

    namespace
    {
        // Takes ownership of file
        std::vector<uint8_t> readAll(HANDLE file)
        {
            std::vector<uint8_t> content;
            LARGE_INTEGER size;
            if ((GetFileSizeEx(file, &size) != 0) and (size.QuadPart > 0))
            {
                content.reserve(static_cast<std::size_t>(size.QuadPart));
            }

            std::vector<uint8_t> chunk(CHUNK_SIZE);
            while (true)
            {
                DWORD count = 0;
                if (ReadFile(file, chunk.data(), CHUNK_SIZE, &count, nullptr) == 0)
                {
                    DWORD error = GetLastError();
                    CloseHandle(file);
                    throw std::system_error(static_cast<int>(error), std::system_category(), LOCATION(": ReadFile() failed"));
                }
                if (count == 0)
                {
                    break;
                }
                content.insert(content.end(), chunk.begin(), chunk.begin() + count);
            }

            CloseHandle(file);
            return content;
        }

        // Takes ownership of file
        void writeAll(HANDLE file, void const* data, std::size_t size)
        {
            auto const* bytes = static_cast<uint8_t const*>(data);
            std::size_t written = 0;
            while (written < size)
            {
                DWORD chunk = CHUNK_SIZE;
                if ((size - written) < CHUNK_SIZE)
                {
                    chunk = static_cast<DWORD>(size - written);
                }

                DWORD count = 0;
                if (WriteFile(file, bytes + written, chunk, &count, nullptr) == 0)
                {
                    DWORD error = GetLastError();
                    CloseHandle(file);
                    throw std::system_error(static_cast<int>(error), std::system_category(), LOCATION(": WriteFile() failed"));
                }
                written += count;
            }

            CloseHandle(file);
        }

        // Closes file and throws if it is not a regular file with a single name. Opened with
        // FILE_FLAG_OPEN_REPARSE_POINT, a symbolic link or a junction is the handle itself and shows in its attributes.
        void checkRegular(HANDLE file)
        {
            if (GetFileType(file) != FILE_TYPE_DISK)
            {
                CloseHandle(file);
                throw std::system_error(ERROR_ACCESS_DENIED, std::system_category(), LOCATION(": not a regular file"));
            }

            BY_HANDLE_FILE_INFORMATION info;
            if (GetFileInformationByHandle(file, &info) == 0)
            {
                DWORD error = GetLastError();
                CloseHandle(file);
                throw std::system_error(static_cast<int>(error), std::system_category(), LOCATION(": GetFileInformationByHandle() failed"));
            }
            if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
            {
                CloseHandle(file);
                throw std::system_error(ERROR_ACCESS_DENIED, std::system_category(), LOCATION(": not a regular file"));
            }
            if (info.nNumberOfLinks > 1)
            {
                CloseHandle(file);
                throw std::system_error(ERROR_ACCESS_DENIED, std::system_category(), LOCATION(": file with several hard links"));
            }
        }
    }

    std::vector<uint8_t> readFile(std::string const& path)
    {
        HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            THROW_LAST_ERROR("CreateFile() for reading failed");
        }
        return readAll(file);
    }

    void writeFile(std::string const& path, void const* data, std::size_t size)
    {
        HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            THROW_LAST_ERROR("CreateFile() for writing failed");
        }
        writeAll(file, data, size);
    }

    std::vector<uint8_t> readRegularFile(std::string const& path)
    {
        HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            THROW_LAST_ERROR("CreateFile() for reading failed");
        }
        checkRegular(file);
        return readAll(file);
    }

    void writeRegularFile(std::string const& path, void const* data, std::size_t size)
    {
        // CREATE_NEW fails on any existing name, a link included
        std::string temporary = path + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
        HANDLE file = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            THROW_LAST_ERROR("CreateFile() for writing failed");
        }

        try
        {
            writeAll(file, data, size);
        }
        catch (...)
        {
            DeleteFileA(temporary.c_str());
            throw;
        }

        if (MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) == 0)
        {
            DWORD error = GetLastError();
            DeleteFileA(temporary.c_str());
            throw std::system_error(static_cast<int>(error), std::system_category(), LOCATION(": MoveFileEx() failed"));
        }
    }
}
