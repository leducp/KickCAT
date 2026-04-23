#ifndef KICKCAT_MASTER_OD_H
#define KICKCAT_MASTER_OD_H

#include <string>
#include <vector>

#include "kickcat/CoE/OD.h"

namespace kickcat
{
    struct Slave;

    struct MasterIdentity
    {
        uint32_t device_type{0};
        std::string device_name;
        std::string hardware_version;
        std::string software_version;
        uint32_t vendor_id{0};
        uint32_t product_code{0};
        uint32_t revision{0};
        uint32_t serial_number{0};
    };

    /// \brief Builder for the ETG.1510 Master Object Dictionary (ETG.8200-accessible via address 0).
    ///        populate() is one-shot per Dictionary.
    class MasterOD
    {
    public:
        MasterOD(MasterIdentity const& identity);

        /// \brief Build the master dictionary. Ownership is transferred to the caller.
        CoE::Dictionary createDictionary();

        /// \brief Create per-slave 0x8nnn configuration objects and populate them from SII data.
        void populate(CoE::Dictionary& dict, std::vector<Slave> const& slaves);

        struct ConfigurationData
        {
            CoE::Entry* fixed_address{nullptr};
            CoE::Entry* vendor_id{nullptr};
            CoE::Entry* product_code{nullptr};
            CoE::Entry* revision{nullptr};
            CoE::Entry* serial_number{nullptr};
            CoE::Entry* mailbox_out_size{nullptr};
            CoE::Entry* mailbox_in_size{nullptr};
        };

        std::vector<ConfigurationData> const& configurationData() const { return configuration_data_; }

    private:
        MasterIdentity identity_;
        std::vector<ConfigurationData> configuration_data_;
    };
}

#endif
