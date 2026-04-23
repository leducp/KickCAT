#include <gtest/gtest.h>

#include "kickcat/Error.h"
#include "kickcat/MasterOD.h"
#include "kickcat/Mailbox.h"
#include "kickcat/Slave.h"
#include "kickcat/CoE/protocol.h"
#include "kickcat/CoE/mailbox/request.h"

using namespace kickcat;

constexpr uint16_t MBX_SIZE = 256;

static MasterIdentity defaultIdentity()
{
    MasterIdentity id;
    id.device_type       = 0x00001389;
    id.device_name       = "KickCAT";
    id.hardware_version  = "1.0";
    id.software_version  = "2.3.4";
    id.vendor_id         = 0x000006A5;
    id.product_code      = 0x00B0CAD0;
    id.revision          = 0x00000001;
    id.serial_number     = 0xCAFEDECA;
    return id;
}

static std::vector<uint8_t> buildSDOUpload(uint16_t index, uint8_t subindex)
{
    uint32_t data;
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage msg{MBX_SIZE, index, subindex, false, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};

    std::vector<uint8_t> raw(MBX_SIZE, 0);
    std::memcpy(raw.data(), msg.data(), MBX_SIZE);
    return raw;
}

static std::string extractStringFromSDOReply(std::vector<uint8_t> const& reply)
{
    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);
    auto* sdo     = pointData<CoE::ServiceData>(coe);
    auto* payload = pointData<uint8_t>(sdo);

    if (sdo->transfer_type == 1)
    {
        uint32_t size = 4 - sdo->block_size;
        return std::string(reinterpret_cast<char const*>(payload), size);
    }

    uint32_t size;
    std::memcpy(&size, payload, sizeof(size));
    return std::string(reinterpret_cast<char const*>(payload + 4), size);
}


class MasterODTest : public ::testing::Test
{
public:
    void SetUp() override
    {
        MasterOD od(defaultIdentity());
        mbx.enableCoE(od.createDictionary());
    }

    mailbox::response::Mailbox mbx{MBX_SIZE};
};


TEST_F(MasterODTest, read_device_type)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x1000, 0));
    ASSERT_FALSE(reply.empty());

    auto* header = pointData<mailbox::Header>(reply.data());
    auto* coe    = pointData<CoE::Header>(header);
    auto* sdo    = pointData<CoE::ServiceData>(coe);
    auto* payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ(CoE::SDO::response::UPLOAD, sdo->command);
    ASSERT_EQ(0x00001389u, *payload);
}


TEST_F(MasterODTest, read_device_name)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x1008, 0));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ("KickCAT", extractStringFromSDOReply(reply));
}


TEST_F(MasterODTest, read_hardware_version)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x1009, 0));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ("1.0", extractStringFromSDOReply(reply));
}


TEST_F(MasterODTest, read_software_version)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x100A, 0));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ("2.3.4", extractStringFromSDOReply(reply));
}


TEST_F(MasterODTest, read_identity_vendor_id)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x1018, 1));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);
    auto* sdo     = pointData<CoE::ServiceData>(coe);
    auto* payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ(0x000006A5u, *payload);
}


TEST_F(MasterODTest, read_identity_product_code)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x1018, 2));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);
    auto* sdo     = pointData<CoE::ServiceData>(coe);
    auto* payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ(0x00B0CAD0u, *payload);
}


TEST_F(MasterODTest, read_identity_revision)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x1018, 3));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);
    auto* sdo     = pointData<CoE::ServiceData>(coe);
    auto* payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ(0x00000001u, *payload);
}


TEST_F(MasterODTest, read_identity_serial_number)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x1018, 4));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);
    auto* sdo     = pointData<CoE::ServiceData>(coe);
    auto* payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ(0xCAFEDECAu, *payload);
}


TEST_F(MasterODTest, read_nonexistent_object_returns_abort)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x9999, 0));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);
    auto* sdo     = pointData<CoE::ServiceData>(coe);
    auto* payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
    ASSERT_EQ(CoE::SDO::request::ABORT, sdo->command);
    ASSERT_EQ(CoE::SDO::abort::OBJECT_DOES_NOT_EXIST, *payload);
}


TEST_F(MasterODTest, read_nonexistent_subindex_returns_abort)
{
    auto reply = mbx.processRequest(buildSDOUpload(0x1018, 99));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);
    auto* sdo     = pointData<CoE::ServiceData>(coe);
    auto* payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
    ASSERT_EQ(CoE::SDO::request::ABORT, sdo->command);
    ASSERT_EQ(CoE::SDO::abort::SUBINDEX_DOES_NOT_EXIST, *payload);
}


TEST_F(MasterODTest, optional_strings_omitted_when_empty)
{
    MasterIdentity minimal{};
    minimal.vendor_id = 0x42;
    MasterOD od_minimal(minimal);

    mailbox::response::Mailbox mbx_minimal{MBX_SIZE};
    mbx_minimal.enableCoE(od_minimal.createDictionary());

    // 0x1008 should not exist
    auto reply = mbx_minimal.processRequest(buildSDOUpload(0x1008, 0));
    ASSERT_FALSE(reply.empty());

    auto* header  = pointData<mailbox::Header>(reply.data());
    auto* coe     = pointData<CoE::Header>(header);
    auto* sdo     = pointData<CoE::ServiceData>(coe);
    auto* payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::SDO::request::ABORT, sdo->command);
    ASSERT_EQ(CoE::SDO::abort::OBJECT_DOES_NOT_EXIST, *payload);

    // But 0x1000 and 0x1018 should still exist
    auto reply_type = mbx_minimal.processRequest(buildSDOUpload(0x1000, 0));
    auto* coe_type = pointData<CoE::Header>(pointData<mailbox::Header>(reply_type.data()));
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_type->service);

    auto reply_id = mbx_minimal.processRequest(buildSDOUpload(0x1018, 1));
    auto* header_id = pointData<mailbox::Header>(reply_id.data());
    auto* coe_id = pointData<CoE::Header>(header_id);
    auto* sdo_id = pointData<CoE::ServiceData>(coe_id);
    auto* payload_id = pointData<uint32_t>(sdo_id);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_id->service);
    ASSERT_EQ(0x42u, *payload_id);
}


TEST(MasterODPopulate, creates_config_objects_from_sii)
{
    MasterIdentity id;
    id.vendor_id = 0x42;
    MasterOD od(id);

    std::vector<Slave> slaves(2);
    slaves[0].address = 0x1001;
    slaves[0].sii.info.vendor_id       = 0xDEADBEEF;
    slaves[0].sii.info.product_code    = 0xCAFE;
    slaves[0].sii.info.revision_number = 0x0003;
    slaves[0].sii.info.serial_number   = 0x12345678;
    slaves[0].sii.info.standard_recv_mbx_size = 512;
    slaves[0].sii.info.standard_send_mbx_size = 256;

    slaves[1].address = 0x1002;
    slaves[1].sii.info.vendor_id       = 0xBAADF00D;
    slaves[1].sii.info.product_code    = 0xBEEF;
    slaves[1].sii.info.revision_number = 0x0001;
    slaves[1].sii.info.serial_number   = 0xABCD;
    slaves[1].sii.info.standard_recv_mbx_size = 128;
    slaves[1].sii.info.standard_send_mbx_size = 128;

    auto dict = od.createDictionary();
    od.populate(dict, slaves);

    mailbox::response::Mailbox mbx{MBX_SIZE};
    mbx.enableCoE(std::move(dict));

    auto const& configs = od.configurationData();
    ASSERT_EQ(2u, configs.size());

    // Read slave 0 vendor id (0x8000, subindex 5)
    auto reply = mbx.processRequest(buildSDOUpload(0x8000, 5));
    ASSERT_FALSE(reply.empty());
    auto* coe = pointData<CoE::Header>(pointData<mailbox::Header>(reply.data()));
    auto* sdo = pointData<CoE::ServiceData>(coe);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ(0xDEADBEEFu, *pointData<uint32_t>(sdo));

    // Read slave 0 fixed address (0x8000, subindex 1)
    auto reply_addr = mbx.processRequest(buildSDOUpload(0x8000, 1));
    ASSERT_FALSE(reply_addr.empty());
    auto* coe_addr = pointData<CoE::Header>(pointData<mailbox::Header>(reply_addr.data()));
    auto* sdo_addr = pointData<CoE::ServiceData>(coe_addr);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_addr->service);
    ASSERT_EQ(0x1001u, *pointData<uint16_t>(sdo_addr));

    // Read slave 1 vendor id (0x8001, subindex 5)
    auto reply_s1 = mbx.processRequest(buildSDOUpload(0x8001, 5));
    ASSERT_FALSE(reply_s1.empty());
    auto* coe_s1 = pointData<CoE::Header>(pointData<mailbox::Header>(reply_s1.data()));
    auto* sdo_s1 = pointData<CoE::ServiceData>(coe_s1);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_s1->service);
    ASSERT_EQ(0xBAADF00Du, *pointData<uint32_t>(sdo_s1));

    // Read slave 1 mailbox out size (0x8001, subindex 33)
    auto reply_mbx = mbx.processRequest(buildSDOUpload(0x8001, 33));
    ASSERT_FALSE(reply_mbx.empty());
    auto* coe_mbx = pointData<CoE::Header>(pointData<mailbox::Header>(reply_mbx.data()));
    auto* sdo_mbx = pointData<CoE::ServiceData>(coe_mbx);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_mbx->service);
    ASSERT_EQ(128u, *pointData<uint16_t>(sdo_mbx));
}


TEST(MasterODPopulate, hole_subindex_aborts_cleanly)
{
    // Probe the sparse-RECORD guard: an individual SDO upload of a subindex in a gap (between
    // :08 and :33, or above :40) must return a clean abort, not crash.
    MasterIdentity id;
    MasterOD od(id);

    std::vector<Slave> slaves(1);
    auto dict = od.createDictionary();
    od.populate(dict, slaves);

    mailbox::response::Mailbox mbx{MBX_SIZE};
    mbx.enableCoE(std::move(dict));

    for (uint8_t hole : {uint8_t{9}, uint8_t{32}, uint8_t{41}})
    {
        auto reply = mbx.processRequest(buildSDOUpload(0x8000, hole));
        ASSERT_FALSE(reply.empty());
        auto* coe = pointData<CoE::Header>(pointData<mailbox::Header>(reply.data()));
        auto* sdo = pointData<CoE::ServiceData>(coe);
        EXPECT_EQ(CoE::Service::SDO_REQUEST, coe->service);
        EXPECT_EQ(CoE::SDO::request::ABORT,  sdo->command);
    }
}


TEST(MasterODPopulate, double_populate_throws)
{
    // Calling populate() twice on the same dict would leave stale 0x8nnn entries and invalidate
    // previously-captured ConfigurationData pointers. Guard against the mistake.
    MasterIdentity id;
    MasterOD od(id);

    std::vector<Slave> slaves(1);
    auto dict = od.createDictionary();
    od.populate(dict, slaves);

    EXPECT_THROW(od.populate(dict, slaves), kickcat::Error);
}


TEST(MasterODPopulate, entry_pointers_survive_dictionary_move)
{
    MasterIdentity id;
    MasterOD od(id);

    std::vector<Slave> slaves(2);
    slaves[0].sii.info.vendor_id = 0x11;
    slaves[1].sii.info.vendor_id = 0x22;

    auto dict = od.createDictionary();
    od.populate(dict, slaves);

    mailbox::response::Mailbox mbx{MBX_SIZE};
    mbx.enableCoE(std::move(dict));

    auto const& configs = od.configurationData();
    ASSERT_EQ(2u, configs.size());
    ASSERT_NE(nullptr, configs[0].vendor_id);
    ASSERT_NE(nullptr, configs[1].vendor_id);

    ASSERT_EQ(0x11u, *static_cast<uint32_t*>(configs[0].vendor_id->data));
    ASSERT_EQ(0x22u, *static_cast<uint32_t*>(configs[1].vendor_id->data));
}


TEST(MasterODPopulate, number_of_entries_is_highest_supported_subindex)
{
    // All ETG.1510 Table 9 entries are populated, up to :40 (Diag History Object Supported).
    // Subindex 0 reports the largest supported subindex per CiA-301 RECORD convention.
    MasterIdentity id;
    MasterOD od(id);

    std::vector<Slave> slaves(1);
    auto dict = od.createDictionary();
    od.populate(dict, slaves);

    mailbox::response::Mailbox mbx{MBX_SIZE};
    mbx.enableCoE(std::move(dict));

    auto reply = mbx.processRequest(buildSDOUpload(0x8000, 0));
    ASSERT_FALSE(reply.empty());
    auto* coe = pointData<CoE::Header>(pointData<mailbox::Header>(reply.data()));
    auto* sdo = pointData<CoE::ServiceData>(coe);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ(40u, *pointData<uint8_t>(sdo));
}


TEST(MasterODPopulate, optional_subindices_exist_with_defaults)
{
    // :02, :04, :35, :38, :40 are optional per ETG.1510 Table 9. We populate them with safe
    // defaults so the structure is complete and later PRs can feed real values.
    MasterIdentity id;
    MasterOD od(id);

    std::vector<Slave> slaves(1);
    slaves[0].address = 0x1001;
    slaves[0].sii.strings = {"AD-4242"};
    slaves[0].sii.general.device_order_id = 1;
    slaves[0].sii.general.port_0 = 1;   // MII
    slaves[0].sii.general.port_1 = 2;   // EBUS
    slaves[0].sii.general.port_2 = 0;
    slaves[0].sii.general.port_3 = 0;

    auto dict = od.createDictionary();
    od.populate(dict, slaves);

    mailbox::response::Mailbox mbx{MBX_SIZE};
    mbx.enableCoE(std::move(dict));

    // :02 Type — from SII general order string
    auto reply_type = mbx.processRequest(buildSDOUpload(0x8000, 2));
    ASSERT_FALSE(reply_type.empty());
    auto* coe_type = pointData<CoE::Header>(pointData<mailbox::Header>(reply_type.data()));
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_type->service);
    ASSERT_EQ("AD-4242", extractStringFromSDOReply(reply_type));

    // :04 Device Type — default 0 until ENI / slave 0x1000 SDO upload is wired in
    auto reply_devtype = mbx.processRequest(buildSDOUpload(0x8000, 4));
    auto* coe_devtype  = pointData<CoE::Header>(pointData<mailbox::Header>(reply_devtype.data()));
    auto* sdo_devtype  = pointData<CoE::ServiceData>(coe_devtype);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_devtype->service);
    ASSERT_EQ(0u, *pointData<uint32_t>(sdo_devtype));

    // :35 Link Status — obsolete per spec, populated as 0 for legacy-tool compatibility
    auto reply_ls = mbx.processRequest(buildSDOUpload(0x8000, 35));
    auto* coe_ls  = pointData<CoE::Header>(pointData<mailbox::Header>(reply_ls.data()));
    auto* sdo_ls  = pointData<CoE::ServiceData>(coe_ls);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_ls->service);
    ASSERT_EQ(0u, *pointData<uint8_t>(sdo_ls));

    // :38 Port Physics — reconstituted from SII general.port_0..port_3 (4-bit nibbles)
    auto reply_pp = mbx.processRequest(buildSDOUpload(0x8000, 38));
    auto* coe_pp  = pointData<CoE::Header>(pointData<mailbox::Header>(reply_pp.data()));
    auto* sdo_pp  = pointData<CoE::ServiceData>(coe_pp);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_pp->service);
    ASSERT_EQ(uint16_t{0x0021}, *pointData<uint16_t>(sdo_pp)); // port_0=1, port_1=2 → low byte 0x21

    // :40 Diag History Object Supported — default false (no slave OD probe yet)
    auto reply_dh = mbx.processRequest(buildSDOUpload(0x8000, 40));
    auto* coe_dh  = pointData<CoE::Header>(pointData<mailbox::Header>(reply_dh.data()));
    auto* sdo_dh  = pointData<CoE::ServiceData>(coe_dh);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_dh->service);
    ASSERT_EQ(0u, *pointData<uint8_t>(sdo_dh));
}


TEST(MasterODPopulate, mandatory_subindices_from_spec)
{
    // Mandatory per ETG.1510 Table 9 beyond what PR2 landed: :03 Name, :36 Link Preset,
    // :37 Flags, :39 Mailbox Protocols Supported.
    MasterIdentity id;
    MasterOD od(id);

    std::vector<Slave> slaves(1);
    // Point device_name_id at the first SII string (1-based index per ETG.2010).
    slaves[0].sii.strings = {"Acme Drive"};
    slaves[0].sii.general.device_name_id = 1;
    slaves[0].sii.info.mailbox_protocol = eeprom::MailboxProtocol::CoE | eeprom::MailboxProtocol::FoE;

    auto dict = od.createDictionary();
    od.populate(dict, slaves);

    mailbox::response::Mailbox mbx{MBX_SIZE};
    mbx.enableCoE(std::move(dict));

    // :03 Name
    auto reply_name = mbx.processRequest(buildSDOUpload(0x8000, 3));
    ASSERT_FALSE(reply_name.empty());
    auto* coe_name = pointData<CoE::Header>(pointData<mailbox::Header>(reply_name.data()));
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_name->service);
    ASSERT_EQ("Acme Drive", extractStringFromSDOReply(reply_name));

    // :36 Link Preset (default 0 until ENI is wired in)
    auto reply_lp = mbx.processRequest(buildSDOUpload(0x8000, 36));
    ASSERT_FALSE(reply_lp.empty());
    auto* coe_lp = pointData<CoE::Header>(pointData<mailbox::Header>(reply_lp.data()));
    auto* sdo_lp = pointData<CoE::ServiceData>(coe_lp);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_lp->service);
    ASSERT_EQ(0u, *pointData<uint8_t>(sdo_lp));

    // :37 Flags (default 0 until ENI is wired in)
    auto reply_fl = mbx.processRequest(buildSDOUpload(0x8000, 37));
    ASSERT_FALSE(reply_fl.empty());
    auto* coe_fl = pointData<CoE::Header>(pointData<mailbox::Header>(reply_fl.data()));
    auto* sdo_fl = pointData<CoE::ServiceData>(coe_fl);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_fl->service);
    ASSERT_EQ(0u, *pointData<uint8_t>(sdo_fl));

    // :39 Mailbox Protocols Supported (maps directly from SII mailbox_protocol bitmask)
    auto reply_mp = mbx.processRequest(buildSDOUpload(0x8000, 39));
    ASSERT_FALSE(reply_mp.empty());
    auto* coe_mp = pointData<CoE::Header>(pointData<mailbox::Header>(reply_mp.data()));
    auto* sdo_mp = pointData<CoE::ServiceData>(coe_mp);
    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe_mp->service);
    ASSERT_EQ(static_cast<uint16_t>(eeprom::MailboxProtocol::CoE | eeprom::MailboxProtocol::FoE),
              *pointData<uint16_t>(sdo_mp));
}


TEST(MasterODPopulate, name_falls_back_when_sii_has_no_string)
{
    // With no SII name string, :03 should still exist (spec marks it mandatory) with a sensible default
    // derived from the fixed station address so operators can correlate entries with the bus.
    MasterIdentity id;
    MasterOD od(id);

    std::vector<Slave> slaves(2);
    slaves[0].address = 0x1001;
    slaves[1].address = 0x100A;
    // slaves[i].sii.general.device_name_id stays at 0 (no name)

    auto dict = od.createDictionary();
    od.populate(dict, slaves);

    mailbox::response::Mailbox mbx{MBX_SIZE};
    mbx.enableCoE(std::move(dict));

    auto reply0 = mbx.processRequest(buildSDOUpload(0x8000, 3));
    auto reply1 = mbx.processRequest(buildSDOUpload(0x8001, 3));
    ASSERT_EQ("Slave @0x1001", extractStringFromSDOReply(reply0));
    ASSERT_EQ("Slave @0x100A", extractStringFromSDOReply(reply1));
}
