#include <windows.h>

#include <stdexcept>

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

    std::vector<uint8_t> readFile(std::string const& path)
    {
        HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            THROW_LAST_ERROR("CreateFile() for reading failed");
        }

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

    void writeFile(std::string const& path, void const* data, std::size_t size)
    {
        HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            THROW_LAST_ERROR("CreateFile() for writing failed");
        }

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
}
