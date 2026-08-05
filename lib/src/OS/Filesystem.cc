// \brief OS agnostic filesystem helpers - pure path string handling, no syscall
#include "kickcat/OS/Filesystem.h"

namespace kickcat::filesystem
{
    std::string parent(std::string const& path)
    {
        std::size_t separator = path.find_last_of('/');
        if (separator == std::string::npos)
        {
            return {};
        }
        // Keep the root itself for "/name": trimming to "" would turn it into a relative path.
        if (separator == 0)
        {
            return "/";
        }
        return path.substr(0, separator);
    }

    std::string filename(std::string const& path)
    {
        std::size_t separator = path.find_last_of('/');
        if (separator == std::string::npos)
        {
            return path;
        }
        return path.substr(separator + 1);
    }

    std::string extension(std::string const& path)
    {
        std::string leaf = filename(path);
        std::size_t dot = leaf.find_last_of('.');
        // A leading dot names a hidden file, it does not start an extension.
        if ((dot == std::string::npos) or (dot == 0))
        {
            return {};
        }
        return leaf.substr(dot);
    }

    std::vector<std::string> listFilesRecursive(std::string const& directory)
    {
        std::vector<std::string> files;
        for (auto const& entry : list(directory))
        {
            std::string path = join(directory, entry.name);
            if (entry.is_directory)
            {
                std::vector<std::string> nested = listFilesRecursive(path);
                files.insert(files.end(), nested.begin(), nested.end());
                continue;
            }
            files.push_back(path);
        }
        return files;
    }

    std::string join(std::string const& directory, std::string const& name)
    {
        if (directory.empty())
        {
            return name;
        }
        if (directory.back() == '/')
        {
            return directory + name;
        }
        return directory + "/" + name;
    }

    void writeFile(std::string const& path, std::string const& content)
    {
        writeFile(path, content.data(), content.size());
    }
}
