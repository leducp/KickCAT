#include "kickcat/simulation/DirectoryStorage.h"

#include <algorithm>
#include <vector>

#include "kickcat/FoE/protocol.h"
#include "kickcat/OS/Filesystem.h"

namespace kickcat::sim
{
    namespace
    {
        class Reader final : public FoE::AbstractReader
        {
        public:
            Reader(std::vector<uint8_t> content)
                : content_{std::move(content)}
            {
            }

            uint32_t read(uint8_t* buffer, uint32_t capacity, uint32_t& size) override
            {
                size = static_cast<uint32_t>(std::min<std::size_t>(capacity, content_.size() - offset_));
                std::copy_n(content_.data() + offset_, size, buffer);   // memcpy is undefined on an empty file
                offset_ += size;
                return 0;
            }

        private:
            std::vector<uint8_t> content_;
            std::size_t offset_{0};
        };

        class Writer final : public FoE::AbstractWriter
        {
        public:
            Writer(std::string path, uint32_t max_size)
                : path_{std::move(path)}
                , max_size_{max_size}
            {
            }

            uint32_t write(uint8_t const* data, uint32_t size) override
            {
                if ((content_.size() + size) > max_size_)
                {
                    return FoE::result::DISK_FULL;
                }
                content_.insert(content_.end(), data, data + size);
                return 0;
            }

            uint32_t commit() override
            {
                try
                {
                    // The name comes from the network: a link planted in the directory shall not make the simulator
                    // (possibly privileged) write anything outside of it
                    filesystem::writeRegularFile(path_, content_.data(), content_.size());
                }
                catch (std::exception const&)
                {
                    return FoE::result::ACCESS_DENIED;
                }
                return 0;
            }

        private:
            std::string path_;
            uint32_t max_size_;
            std::vector<uint8_t> content_;
        };
    }


    DirectoryStorage::DirectoryStorage(Config config)
        : config_{std::move(config)}
    {
    }


    std::tuple<uint32_t, std::string> DirectoryStorage::resolve(std::string_view name, uint32_t password) const
    {
        if ((config_.password != 0) and (password != config_.password))
        {
            return {FoE::result::NO_RIGHTS, ""};
        }

        if (name.empty() or (name == ".") or (name == ".."))
        {
            return {FoE::result::ACCESS_DENIED, ""};
        }

        for (char c : name)
        {
            unsigned char u = static_cast<unsigned char>(c);
            if ((c == '/') or (c == '\\') or (c == ':') or (u < 0x20) or (u == 0x7F))
            {
                return {FoE::result::ACCESS_DENIED, ""};
            }
        }

        return {0, filesystem::join(config_.directory, std::string{name})};
    }


    std::tuple<uint32_t, std::unique_ptr<FoE::AbstractReader>> DirectoryStorage::openRead(std::string_view name, uint32_t password)
    {
        auto [rc, path] = resolve(name, password);
        if (rc != 0)
        {
            return {rc, nullptr};
        }

        if ((not filesystem::exists(path)) or filesystem::isDirectory(path))
        {
            return {FoE::result::NOT_FOUND, nullptr};
        }

        try
        {
            // Same as for a write: never follow a link planted in the directory
            return {0, std::make_unique<Reader>(filesystem::readRegularFile(path))};
        }
        catch (std::exception const&)
        {
            return {FoE::result::ACCESS_DENIED, nullptr};
        }
    }


    std::tuple<uint32_t, std::unique_ptr<FoE::AbstractWriter>> DirectoryStorage::openWrite(std::string_view name, uint32_t password)
    {
        auto [rc, path] = resolve(name, password);
        if (rc != 0)
        {
            return {rc, nullptr};
        }

        if (config_.read_only)
        {
            return {FoE::result::ACCESS_DENIED, nullptr};
        }

        return {0, std::make_unique<Writer>(std::move(path), config_.max_size)};
    }
}
