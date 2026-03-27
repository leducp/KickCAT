#include <gtest/gtest.h>

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
    slaves[0].sii.vendor_id       = 0xDEADBEEF;
    slaves[0].sii.product_code    = 0xCAFE;
    slaves[0].sii.revision_number = 0x0003;
    slaves[0].sii.serial_number   = 0x12345678;
    slaves[0].sii.mailbox_recv_size = 512;
    slaves[0].sii.mailbox_send_size = 256;

    slaves[1].address = 0x1002;
    slaves[1].sii.vendor_id       = 0xBAADF00D;
    slaves[1].sii.product_code    = 0xBEEF;
    slaves[1].sii.revision_number = 0x0001;
    slaves[1].sii.serial_number   = 0xABCD;
    slaves[1].sii.mailbox_recv_size = 128;
    slaves[1].sii.mailbox_send_size = 128;

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


TEST(MasterODPopulate, entry_pointers_survive_dictionary_move)
{
    MasterIdentity id;
    MasterOD od(id);

    std::vector<Slave> slaves(2);
    slaves[0].sii.vendor_id = 0x11;
    slaves[1].sii.vendor_id = 0x22;

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
