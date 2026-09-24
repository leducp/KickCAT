#ifndef KICKCAT_SIMULATION_DIRECTORY_STORAGE_H
#define KICKCAT_SIMULATION_DIRECTORY_STORAGE_H

#include <string>

#include "kickcat/FoE/Storage.h"

namespace kickcat::sim
{
    /// \brief FoE storage serving the files of one flat host directory
    /// \details A transfer is staged in memory: a read loads the file on open, a write reaches the disk only on
    ///          commit and replaces the file whole, so an aborted or failed write never leaves a truncated file.
    ///          A read serves only regular files with a single name: symbolic links, hard links and special files
    ///          are refused (ACCESS_DENIED). A write replaces a link by a regular file.
    class DirectoryStorage final : public FoE::AbstractStorage
    {
    public:
        struct Config
        {
            std::string directory;
            uint32_t    password{0};                // 0: any password is accepted
            uint32_t    max_size{64 * 1024 * 1024}; // largest file accepted on write
            bool        read_only{false};
        };

        DirectoryStorage(Config config);
        virtual ~DirectoryStorage() = default;

        std::tuple<uint32_t, std::unique_ptr<FoE::AbstractReader>> openRead(std::string_view name, uint32_t password) override;
        std::tuple<uint32_t, std::unique_ptr<FoE::AbstractWriter>> openWrite(std::string_view name, uint32_t password) override;

    private:
        /// Check the credentials and the name, which shall not reach anything outside the directory
        /// \return 0 and the file path, or an FoE error code
        std::tuple<uint32_t, std::string> resolve(std::string_view name, uint32_t password) const;

        Config config_;
    };
}

#endif
