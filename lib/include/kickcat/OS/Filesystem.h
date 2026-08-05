#ifndef KICKCAT_OS_FILESYSTEM_H
#define KICKCAT_OS_FILESYSTEM_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kickcat::filesystem
{
    // Thin wrappers over the platform's own file API, deliberately not <filesystem>/<fstream>:
    // EmulatedESC is compiled for the embedded targets, whose C++ export has neither, and on MinGW
    // those two headers bind the binary to libstdc++ symbols the runtime DLL it finds may not
    // export (a load-time STATUS_ENTRYPOINT_NOT_FOUND, seen in CI).
    //
    // Paths are byte strings separated by '/', on every platform: the Win32 API accepts '/' too.
    // Nothing here resolves symlinks, expands '..' or makes a path absolute.

    struct Entry
    {
        std::string name;           // leaf name, not a path
        bool        is_directory;
    };

    /// \return true if the path exists, whatever its kind.
    bool exists(std::string const& path);

    /// \return true if the path exists and is a directory.
    bool isDirectory(std::string const& path);

    /// \brief Delete one file.
    /// \return true if it was deleted, false if it was already absent.
    bool removeFile(std::string const& path);

    /// \brief Create one directory. The parent must already exist.
    /// \return true if it was created, false if it was already there.
    bool createDirectory(std::string const& path);

    /// \brief Delete one directory, which must be empty.
    /// \return true if it was deleted, false if it was already absent.
    bool removeDirectory(std::string const& path);

    /// \brief One directory level, in whatever order the OS reports. '.' and '..' are not listed.
    std::vector<Entry> list(std::string const& path);

    /// \brief Every file below directory, as paths prefixed with directory. Directories themselves
    ///        are not listed, and the order is unspecified.
    std::vector<std::string> listFilesRecursive(std::string const& directory);

    /// \return everything before the last separator, empty if the path has none.
    std::string parent(std::string const& path);

    /// \return the leaf name: everything after the last separator.
    std::string filename(std::string const& path);

    /// \return the last '.' of the leaf name and what follows, empty if the leaf has none.
    std::string extension(std::string const& path);

    /// \brief Append name to directory, inserting a separator only where one is missing. An empty
    ///        directory yields name unchanged, so joining onto parent() of a bare filename works.
    std::string join(std::string const& directory, std::string const& name);

    /// \brief Read a whole file.
    std::vector<uint8_t> readFile(std::string const& path);

    /// \brief Create or truncate a file and write it whole.
    void writeFile(std::string const& path, void const* data, std::size_t size);
    void writeFile(std::string const& path, std::string const& content);
}

#endif
