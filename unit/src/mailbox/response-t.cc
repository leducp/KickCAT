#include <gtest/gtest.h>

#include "mocks/ESC.h"

#include "kickcat/Mailbox.h"
#include "kickcat/CoE/mailbox/request.h"
#include "kickcat/CoE/mailbox/response.h"

using namespace kickcat;
using namespace kickcat::mailbox::response;

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;

constexpr uint16_t TEST_MAILBOX_SIZE = 256;

std::vector<uint8_t> createTestReadSDO(uint16_t index, uint8_t subindex, bool CA=false)
{
    uint32_t data;
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage msg{TEST_MAILBOX_SIZE, index, subindex, CA, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};

    std::vector<uint8_t> raw_message;
    raw_message.insert(raw_message.begin(), msg.data(), msg.data() + TEST_MAILBOX_SIZE);
    return raw_message;
}

std::vector<uint8_t> createTestWriteSDO(uint16_t index, uint8_t subindex, uint32_t data, bool CA=false)
{
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage msg{TEST_MAILBOX_SIZE, index, subindex, CA, CoE::SDO::request::DOWNLOAD, &data, &data_size, 1ms};

    std::vector<uint8_t> raw_message;
    raw_message.insert(raw_message.begin(), msg.data(), msg.data() + TEST_MAILBOX_SIZE);
    return raw_message;
}

CoE::Dictionary createTestDictionary()
{
    CoE::Dictionary dictionary;
    {
        CoE::Object object
        {
            0x1018,
            CoE::ObjectCode::ARRAY,
            "Identity Object",
            {}
        };
        CoE::addEntry<uint8_t> (object, 0, 8,  0,   CoE::Access::READ,  static_cast<CoE::DataType>(5),"Subindex 000",     0x4);
        CoE::addEntry<uint32_t>(object, 1, 32, 8,   CoE::Access::READ,  static_cast<CoE::DataType>(7),"Vendor ID",        0x6a5);
        CoE::addEntry<uint32_t>(object, 2, 32, 40,  CoE::Access::READ,  static_cast<CoE::DataType>(7),"Product code",     0xb0cad0);
        CoE::addEntry<uint32_t>(object, 3, 32, 72,  CoE::Access::READ,  static_cast<CoE::DataType>(7),"Revision number",  0x0);
        CoE::addEntry<uint32_t>(object, 4, 32, 104, CoE::Access::READ,  static_cast<CoE::DataType>(7),"Serial number",    0xcafedeca);
        CoE::addEntry<uint32_t>(object, 5, 32, 136, CoE::Access::WRITE, static_cast<CoE::DataType>(7),"Test write only",  0x0b0cad0);
        dictionary.push_back(std::move(object));
    }

    {
        CoE::Object object
        {
            0x7000,
            CoE::ObjectCode::ARRAY,
            "TEST ARRAY",
            {}
        };
        CoE::addEntry<uint8_t> (object, 0, 8,  0,   CoE::Access::READ | CoE::Access::WRITE,  static_cast<CoE::DataType>(5),"Test 0", 5);
        CoE::addEntry<uint32_t>(object, 1, 32, 8,   CoE::Access::READ | CoE::Access::WRITE,  static_cast<CoE::DataType>(7),"Test 1", 1);
        CoE::addEntry<uint32_t>(object, 2, 32, 40,  CoE::Access::READ | CoE::Access::WRITE,  static_cast<CoE::DataType>(7),"Test 2", 2);
        CoE::addEntry<uint32_t>(object, 3, 32, 72,  CoE::Access::READ | CoE::Access::WRITE,  static_cast<CoE::DataType>(7),"Test 3", 3);
        CoE::addEntry<uint32_t>(object, 4, 32, 104, CoE::Access::READ | CoE::Access::WRITE,  static_cast<CoE::DataType>(7),"Test 4", 4);
        dictionary.push_back(std::move(object));
    }

    return dictionary;
}

// Helper function to create SDO information message
std::vector<uint8_t> createTestSDOInfoMessage(uint8_t opcode, void* payload = nullptr, uint16_t payload_size = 0)
{
    std::vector<uint8_t> raw_message(TEST_MAILBOX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw_message.data());
    header->len = sizeof(CoE::Header) + payload_size;
    header->type = mailbox::Type::CoE;

    auto coe = pointData<CoE::Header>(header);
    coe->service = CoE::Service::SDO_INFORMATION;

    auto sdo = pointData<CoE::ServiceDataInfo>(coe);
    sdo->opcode = opcode;
    sdo->incomplete = 0;
    sdo->fragments_left = 0;

    if (payload and payload_size > 0)
    {
        auto data = pointData<uint8_t>(sdo);
        std::memcpy(data, payload, payload_size);
    }

    return raw_message;
}

class Response : public ::testing::Test
{
public:
    void SetUp() override
    {
        mbx.enableCoE(std::move(createTestDictionary()));
    }

    MockESC esc;
    Mailbox mbx{&esc, TEST_MAILBOX_SIZE, 1};
};

TEST_F(Response, not_configured)
{
    for (uint8_t i = 0; i < reg::SM_STATS; ++i)
    {
        EXPECT_CALL(esc, read(reg::SYNC_MANAGER + sizeof(SyncManager) * i, _, sizeof(SyncManager))).WillOnce(Return(0));
    }
    ASSERT_EQ(-EAGAIN, mbx.configure());
}

TEST_F(Response, badly_configured)
{
    for (int i = 0; i < reg::SM_STATS; ++i)
    {
        EXPECT_CALL(esc, read(reg::SYNC_MANAGER + sizeof(SyncManager) * i, _, sizeof(SyncManager)))
            .WillOnce(Invoke([&](uint16_t, void* data, uint16_t)
            {
                SyncManager sm{}; // a non configured SM
                std::memset(&sm, 0, sizeof(SyncManager));
                std::memcpy(data, &sm, sizeof(SyncManager));
                return sizeof(SyncManager);
            }));
    }

    ASSERT_EQ(-EAGAIN, mbx.configure());
}

TEST_F(Response, SDO_read_expedited_OK)
{
    std::vector<uint8_t> raw_message = createTestReadSDO(0x1018, 2);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    ASSERT_EQ(0, response_msg->size()); // message data was transfered in mbx send queue
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
        ASSERT_EQ(10, header->len);
        ASSERT_EQ(0xb0cad0, *payload);
    }
}

TEST_F(Response, SDO_read_object_ENOENT)
{
    std::vector<uint8_t> raw_message = createTestReadSDO(0x2000, 2);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    ASSERT_EQ(0, response_msg->size()); // message data was transfered in mbx send queue
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
        ASSERT_EQ(10, header->len);
        ASSERT_EQ(CoE::SDO::abort::OBJECT_DOES_NOT_EXIST, *payload);
    }
}

TEST_F(Response, SDO_read_entry_ENOENT)
{
    std::vector<uint8_t> raw_message = createTestReadSDO(0x1018, 200);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    ASSERT_EQ(0, response_msg->size()); // message data was transfered in mbx send queue
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
        ASSERT_EQ(10, header->len);
        ASSERT_EQ(CoE::SDO::abort::SUBINDEX_DOES_NOT_EXIST, *payload);
    }
}

TEST_F(Response, SDO_wrong_size)
{
    std::vector<uint8_t> raw_message = createTestReadSDO(0x1018, 200);
    {
        auto header  = pointData<mailbox::Header>(raw_message.data());
        header->len = 1;
    }
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    ASSERT_EQ(0, response_msg->size()); // message data was transfered in mbx send queue
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto err    = pointData<mailbox::Error::ServiceData>(header);

        ASSERT_EQ(mailbox::ERR, header->type);
        ASSERT_EQ(4, header->len);
        ASSERT_EQ(0x1, err->type);
        ASSERT_EQ(mailbox::Error::SIZE_TOO_SHORT, err->detail);
    }
}

TEST_F(Response, SDO_read_CA)
{
    std::vector<uint8_t> raw_message = createTestReadSDO(0x1018, 0, true);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    ASSERT_EQ(0, response_msg->size()); // message data was transfered in mbx send queue
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto size    = pointData<uint32_t>(sdo);
        auto subidx  = pointData<uint8_t>(size);
        auto entry   = pointData<uint32_t>(subidx);

        ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
        ASSERT_EQ(27, header->len); // 10 + 17
        ASSERT_EQ(17, *size);
        ASSERT_EQ(4,  *subidx);
        ASSERT_EQ(0x6a5,     entry[0]);
        ASSERT_EQ(0xb0cad0,  entry[1]);
        ASSERT_EQ(0,         entry[2]);
        ASSERT_EQ(0xcafedeca,entry[3]);
    }
}

TEST_F(Response, SDO_wrong_type)
{
    std::vector<uint8_t> raw_message = createTestReadSDO(0x1018, 0, true);
    auto header  = pointData<mailbox::Header>(raw_message.data());
    header->type = mailbox::Type::VoE;

    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));
    ASSERT_EQ(nullptr, response_msg);
}

TEST_F(Response, SDO_write_expedited_OK)
{
    std::vector<uint8_t> raw_message = createTestWriteSDO(0x1018, 5, 0x50ABCD05);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    ASSERT_EQ(0, response_msg->size()); // message data was transfered in mbx send queue
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
        ASSERT_EQ(10, header->len);
        ASSERT_EQ(0x50ABCD05, *payload);

        uint32_t data;
        std::memcpy(&data, mbx.getDictionary().at(0).entries.at(5).data, sizeof(uint32_t));
        ASSERT_EQ(data, 0x50ABCD05);
    }
}


TEST_F(Response, SDO_write_complete_OK)
{
    uint32_t data[4] = { 0xCAFEDECA, 0xFF00AA00, 0x12345678, 0xDEADBEEF};
    uint32_t data_size = sizeof(data);
    std::vector<uint8_t> raw_message;
    {
        mailbox::request::SDOMessage msg{TEST_MAILBOX_SIZE, 0x7000, 1, true, CoE::SDO::request::DOWNLOAD, &data, &data_size, 1ms};

        raw_message.insert(raw_message.begin(), msg.data(), msg.data() + TEST_MAILBOX_SIZE);
    }
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    ASSERT_EQ(0, response_msg->size()); // message data was transfered in mbx send queue
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);

        ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
        ASSERT_EQ(CoE::SDO::response::DOWNLOAD, sdo->command);
        ASSERT_EQ(10, header->len);

        auto const& entries = mbx.getDictionary().at(1).entries;
        ASSERT_EQ(*reinterpret_cast<uint32_t*>(entries.at(1).data), 0xCAFEDECA);
        ASSERT_EQ(*reinterpret_cast<uint32_t*>(entries.at(2).data), 0xFF00AA00);
        ASSERT_EQ(*reinterpret_cast<uint32_t*>(entries.at(3).data), 0x12345678);
        ASSERT_EQ(*reinterpret_cast<uint32_t*>(entries.at(4).data), 0xDEADBEEF);
    }
}

TEST_F(Response, SDO_read_non_expedited)
{
    // Create a dictionary entry with size > 4 bytes (8 bytes)
    CoE::Dictionary dict;
    {
        CoE::Object object
        {
            0x8000,
            CoE::ObjectCode::ARRAY,
            "Large Object",
            {}
        };
        uint64_t large_data = 0x123456789ABCDEF0;
        CoE::addEntry<uint64_t>(object, 0, 64, 0, CoE::Access::READ, static_cast<CoE::DataType>(0x001B), "Large Entry", large_data);
        dict.push_back(std::move(object));
    }
    mbx.enableCoE(std::move(dict));

    std::vector<uint8_t> raw_message = createTestReadSDO(0x8000, 0);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto size    = pointData<uint32_t>(sdo);
        auto payload = pointData<uint64_t>(size);

        ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
        ASSERT_EQ(CoE::SDO::response::UPLOAD, sdo->command);
        ASSERT_EQ(0, sdo->transfer_type); // non-expedited
        ASSERT_EQ(18, header->len); // 6 (header) + 4 (size) + 8 (data)
        ASSERT_EQ(8, *size);
        ASSERT_EQ(0x123456789ABCDEF0, *payload);
    }
}

TEST_F(Response, SDO_write_non_expedited)
{
    // Create a dictionary entry with size > 4 bytes (8 bytes)
    CoE::Dictionary dict;
    {
        CoE::Object object
        {
            0x8000,
            CoE::ObjectCode::ARRAY,
            "Large Object",
            {}
        };
        uint64_t large_data = 0;
        CoE::addEntry<uint64_t>(object, 0, 64, 0, CoE::Access::WRITE, static_cast<CoE::DataType>(0x001B), "Large Entry", large_data);
        dict.push_back(std::move(object));
    }
    mbx.enableCoE(std::move(dict));

    // Create a non-expedited download message
    std::vector<uint8_t> raw_message(TEST_MAILBOX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw_message.data());
    header->len = 18; // 6 (header) + 4 (size) + 8 (data)
    header->type = mailbox::Type::CoE;

    auto coe = pointData<CoE::Header>(header);
    coe->service = CoE::Service::SDO_REQUEST;

    auto sdo = pointData<CoE::ServiceData>(coe);
    sdo->command = CoE::SDO::request::DOWNLOAD;
    sdo->transfer_type = 0; // non-expedited
    sdo->index = 0x8000;
    sdo->subindex = 0;

    auto size = pointData<uint32_t>(sdo);
    *size = 8;

    auto payload = pointData<uint64_t>(size);
    *payload = 0xDEADBEEFCAFEBABE;

    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());

    auto const& msg = mbx.readyToSend();
    {
        auto resp_header = pointData<mailbox::Header>(msg.data());
        auto resp_coe = pointData<CoE::Header>(resp_header);
        auto resp_sdo = pointData<CoE::ServiceData>(resp_coe);

        ASSERT_EQ(CoE::Service::SDO_RESPONSE, resp_coe->service);
        ASSERT_EQ(CoE::SDO::response::DOWNLOAD, resp_sdo->command);

        // Verify data was written
        auto const& entries = mbx.getDictionary().at(0).entries;
        uint64_t written_data;
        std::memcpy(&written_data, entries.at(0).data, sizeof(uint64_t));
        ASSERT_EQ(0xDEADBEEFCAFEBABE, written_data);
    }
}

TEST_F(Response, SDO_read_unauthorized)
{
    // Create a write-only entry
    CoE::Dictionary dict;
    {
        CoE::Object object
        {
            0x9000,
            CoE::ObjectCode::ARRAY,
            "Write Only Object",
            {}
        };
        CoE::addEntry<uint32_t>(object, 0, 32, 0, CoE::Access::WRITE, static_cast<CoE::DataType>(7), "Write Only", 0);
        dict.push_back(std::move(object));
    }
    mbx.enableCoE(std::move(dict));

    std::vector<uint8_t> raw_message = createTestReadSDO(0x9000, 0);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
        ASSERT_EQ(CoE::SDO::request::ABORT, sdo->command);
        ASSERT_EQ(CoE::SDO::abort::READ_WRITE_ONLY_ACCESS, *payload);
    }
}

TEST_F(Response, SDO_write_unauthorized)
{
    std::vector<uint8_t> raw_message = createTestWriteSDO(0x1018, 1, 0x12345678);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
        ASSERT_EQ(CoE::SDO::request::ABORT, sdo->command);
        ASSERT_EQ(CoE::SDO::abort::WRITE_READ_ONLY_ACCESS, *payload);
    }
}

TEST_F(Response, SDO_CA_subindex_too_high)
{
    std::vector<uint8_t> raw_message = createTestReadSDO(0x1018, 2, true);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
        ASSERT_EQ(CoE::SDO::request::ABORT, sdo->command);
        ASSERT_EQ(CoE::SDO::abort::UNSUPPORTED_ACCESS, *payload);
    }
}

TEST_F(Response, SDO_write_size_mismatch)
{
    std::vector<uint8_t> raw_message = createTestWriteSDO(0x7000, 1, 0x12345678);
    // Modify the size to be wrong (should be 4, but set to 2)
    {
        auto header = pointData<mailbox::Header>(raw_message.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceData>(coe);
        sdo->block_size = 2; // This makes size = 4 - 2 = 2, but entry expects 4
    }
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
        ASSERT_EQ(CoE::SDO::request::ABORT, sdo->command);
        ASSERT_EQ(CoE::SDO::abort::DATA_TYPE_LENGTH_MISMATCH, *payload);
    }
}

TEST_F(Response, SDO_read_CA_unauthorized_entry)
{
    // Create object with mix of read and write-only entries
    CoE::Dictionary dict;
    {
        CoE::Object object
        {
            0xA000,
            CoE::ObjectCode::ARRAY,
            "Mixed Access Object",
            {}
        };
        CoE::addEntry<uint8_t>(object, 0, 8, 0, CoE::Access::READ | CoE::Access::WRITE, static_cast<CoE::DataType>(5), "Subindex 0", 5);
        CoE::addEntry<uint32_t>(object, 1, 32, 8, CoE::Access::READ, static_cast<CoE::DataType>(7), "Read Only", 0x12345678);
        CoE::addEntry<uint32_t>(object, 2, 32, 40, CoE::Access::WRITE, static_cast<CoE::DataType>(7), "Write Only", 0);
        dict.push_back(std::move(object));
    }
    mbx.enableCoE(std::move(dict));

    std::vector<uint8_t> raw_message = createTestReadSDO(0xA000, 0, true);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        // Should abort because entry 2 is write-only
        ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
        ASSERT_EQ(CoE::SDO::request::ABORT, sdo->command);
        ASSERT_EQ(CoE::SDO::abort::READ_WRITE_ONLY_ACCESS, *payload);
    }
}

TEST_F(Response, SDO_write_CA_unauthorized_entry)
{
    // Create object with mix of read and write-only entries
    CoE::Dictionary dict;
    {
        CoE::Object object
        {
            0xB000,
            CoE::ObjectCode::ARRAY,
            "Mixed Access Object",
            {}
        };
        CoE::addEntry<uint8_t>(object, 0, 8, 0, CoE::Access::READ | CoE::Access::WRITE, static_cast<CoE::DataType>(5), "Subindex 0", 5);
        CoE::addEntry<uint32_t>(object, 1, 32, 8, CoE::Access::READ, static_cast<CoE::DataType>(7), "Read Only", 0x12345678);
        CoE::addEntry<uint32_t>(object, 2, 32, 40, CoE::Access::WRITE, static_cast<CoE::DataType>(7), "Write Only", 0);
        dict.push_back(std::move(object));
    }
    mbx.enableCoE(std::move(dict));

    uint32_t data[3] = {5, 0x11111111, 0x22222222};
    uint32_t data_size = sizeof(data);
    std::vector<uint8_t> raw_message;
    {
        mailbox::request::SDOMessage msg{TEST_MAILBOX_SIZE, 0xB000, 0, true, CoE::SDO::request::DOWNLOAD, &data, &data_size, 1ms};
        raw_message.insert(raw_message.begin(), msg.data(), msg.data() + TEST_MAILBOX_SIZE);
    }
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);
        auto payload = pointData<uint32_t>(sdo);

        // Should abort because entry 1 is read-only
        ASSERT_EQ(CoE::Service::SDO_REQUEST, coe->service);
        ASSERT_EQ(CoE::SDO::request::ABORT, sdo->command);
        ASSERT_EQ(CoE::SDO::abort::WRITE_READ_ONLY_ACCESS, *payload);
    }
}

TEST_F(Response, SDO_write_CA_subindex_0)
{
    uint32_t data[4] = {5, 0x11111111, 0x22222222, 0x33333333};
    uint32_t data_size = sizeof(data);
    std::vector<uint8_t> raw_message;
    {
        mailbox::request::SDOMessage msg{TEST_MAILBOX_SIZE, 0x7000, 0, true, CoE::SDO::request::DOWNLOAD, &data, &data_size, 1ms};
        raw_message.insert(raw_message.begin(), msg.data(), msg.data() + TEST_MAILBOX_SIZE);
    }
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header  = pointData<mailbox::Header>(msg.data());
        auto coe     = pointData<CoE::Header>(header);
        auto sdo     = pointData<CoE::ServiceData>(coe);

        ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
        ASSERT_EQ(CoE::SDO::response::DOWNLOAD, sdo->command);

        // Verify subindex 0 was written
        auto const& entries = mbx.getDictionary().at(1).entries;
        ASSERT_EQ(5, *reinterpret_cast<uint8_t*>(entries.at(0).data));
    }
}

TEST_F(Response, SDO_info_OD_list_number)
{
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_OD_LIST_REQ);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto list_type = pointData<CoE::SDO::information::ListType>(sdo);
        auto data = pointData<uint16_t>(list_type);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::GET_OD_LIST_RESP, sdo->opcode);
        ASSERT_EQ(CoE::SDO::information::ListType::NUMBER, *list_type);
        ASSERT_EQ(2, data[0]); // all_size
        ASSERT_EQ(0, data[1]); // rxpdo_size
        ASSERT_EQ(0, data[2]); // txpdo_size
        ASSERT_EQ(0, data[3]); // backup_size
        ASSERT_EQ(0, data[4]); // settings_size
    }
}

TEST_F(Response, SDO_info_OD_list_all)
{
    CoE::SDO::information::ListType list_type = CoE::SDO::information::ListType::ALL;
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_OD_LIST_REQ, &list_type, sizeof(list_type));
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto resp_list_type = pointData<CoE::SDO::information::ListType>(sdo);
        auto data = pointData<uint16_t>(resp_list_type);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::GET_OD_LIST_RESP, sdo->opcode);
        ASSERT_EQ(CoE::SDO::information::ListType::ALL, *resp_list_type);
        // Should contain both object indices
        ASSERT_EQ(0x1018, data[0]);
        ASSERT_EQ(0x7000, data[1]);
    }
}

TEST_F(Response, SDO_info_OD_req)
{
    uint16_t index = 0x1018;
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_OD_REQ, &index, sizeof(index));
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto desc = pointData<CoE::SDO::information::ObjectDescription>(sdo);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::GET_OD_RESP, sdo->opcode);
        ASSERT_EQ(0x1018, desc->index);
        ASSERT_EQ(CoE::ObjectCode::ARRAY, desc->object_code);
        ASSERT_EQ(5, desc->max_subindex);
    }
}

TEST_F(Response, SDO_info_OD_req_not_found)
{
    uint16_t index = 0x9999;
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_OD_REQ, &index, sizeof(index));
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::SDO_INFO_ERROR_REQ, sdo->opcode);
        ASSERT_EQ(CoE::SDO::abort::OBJECT_DOES_NOT_EXIST, *payload);
    }
}

TEST_F(Response, SDO_info_ED_req)
{
    CoE::SDO::information::EntryDescriptionRequest req;
    req.index = 0x1018;
    req.subindex = 2;
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_ED_REQ, &req, sizeof(req));
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto desc = pointData<CoE::SDO::information::EntryDescription>(sdo);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::GET_ED_RESP, sdo->opcode);
        ASSERT_EQ(0x1018, desc->index);
        ASSERT_EQ(2, desc->subindex);
        ASSERT_EQ(32, desc->bit_length);
        ASSERT_EQ(CoE::Access::READ, desc->access);
    }
}

TEST_F(Response, SDO_info_ED_req_not_found)
{
    CoE::SDO::information::EntryDescriptionRequest req;
    req.index = 0x1018;
    req.subindex = 99;
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_ED_REQ, &req, sizeof(req));
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::SDO_INFO_ERROR_REQ, sdo->opcode);
        ASSERT_EQ(CoE::SDO::abort::SUBINDEX_DOES_NOT_EXIST, *payload);
    }
}

TEST_F(Response, SDO_info_invalid_opcode)
{
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(0xFF); // Invalid opcode
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto payload = pointData<uint32_t>(sdo);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::SDO_INFO_ERROR_REQ, sdo->opcode);
        ASSERT_EQ(CoE::SDO::abort::COMMAND_SPECIFIER_INVALID, *payload);
    }
}

TEST_F(Response, createSDOMessage_emergency)
{
    std::vector<uint8_t> raw_message(TEST_MAILBOX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw_message.data());
    header->type = mailbox::Type::CoE;
    auto coe = pointData<CoE::Header>(header);
    coe->service = CoE::Service::EMERGENCY;

    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));
    ASSERT_EQ(nullptr, response_msg);
}

TEST_F(Response, createSDOMessage_SDO_response)
{
    std::vector<uint8_t> raw_message(TEST_MAILBOX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw_message.data());
    header->type = mailbox::Type::CoE;
    auto coe = pointData<CoE::Header>(header);
    coe->service = CoE::Service::SDO_RESPONSE;

    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));
    ASSERT_EQ(nullptr, response_msg);
}

TEST_F(Response, createSDOMessage_invalid_service)
{
    std::vector<uint8_t> raw_message(TEST_MAILBOX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw_message.data());
    header->type = mailbox::Type::CoE;
    auto coe = pointData<CoE::Header>(header);
    coe->service = 0x0F; // Invalid service (max value for 4-bit field, not in enum)

    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));
    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());

    auto const& msg = mbx.readyToSend();
    {
        auto resp_header = pointData<mailbox::Header>(msg.data());
        auto err = pointData<mailbox::Error::ServiceData>(resp_header);

        ASSERT_EQ(mailbox::ERR, resp_header->type);
        ASSERT_EQ(mailbox::Error::INVALID_HEADER, err->detail);
    }
}

TEST_F(Response, SDO_invalid_command)
{
    std::vector<uint8_t> raw_message = createTestReadSDO(0x1018, 2);
    {
        auto header = pointData<mailbox::Header>(raw_message.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceData>(coe);
        sdo->command = 0x07; // Invalid command (max value for 3-bit field, not in enum)
    }
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_EQ(mailbox::ProcessingResult::NOOP, response_msg->process());
}

TEST_F(Response, SDO_process_with_raw_message)
{
    std::vector<uint8_t> raw_message = createTestReadSDO(0x1018, 2);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    std::vector<uint8_t> dummy_message(TEST_MAILBOX_SIZE, 0);
    ASSERT_EQ(mailbox::ProcessingResult::NOOP, response_msg->process(dummy_message));
}

TEST_F(Response, SDO_info_process_with_raw_message)
{
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_OD_LIST_REQ);
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    std::vector<uint8_t> dummy_message(TEST_MAILBOX_SIZE, 0);
    ASSERT_EQ(mailbox::ProcessingResult::NOOP, response_msg->process(dummy_message));
}

TEST_F(Response, SDO_info_OD_list_rxpdo)
{
    CoE::SDO::information::ListType list_type = CoE::SDO::information::ListType::RxPDO;
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_OD_LIST_REQ, &list_type, sizeof(list_type));
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto resp_list_type = pointData<CoE::SDO::information::ListType>(sdo);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::GET_OD_LIST_RESP, sdo->opcode);
        ASSERT_EQ(CoE::SDO::information::ListType::RxPDO, *resp_list_type);
        // Should be empty since no objects have RxPDO access
    }
}

TEST_F(Response, SDO_info_OD_list_txpdo)
{
    CoE::SDO::information::ListType list_type = CoE::SDO::information::ListType::TxPDO;
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_OD_LIST_REQ, &list_type, sizeof(list_type));
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto resp_list_type = pointData<CoE::SDO::information::ListType>(sdo);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::GET_OD_LIST_RESP, sdo->opcode);
        ASSERT_EQ(CoE::SDO::information::ListType::TxPDO, *resp_list_type);
        // Should be empty since no objects have TxPDO access
    }
}

TEST_F(Response, SDO_info_OD_list_backup)
{
    CoE::SDO::information::ListType list_type = CoE::SDO::information::ListType::BACKUP;
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_OD_LIST_REQ, &list_type, sizeof(list_type));
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto resp_list_type = pointData<CoE::SDO::information::ListType>(sdo);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::GET_OD_LIST_RESP, sdo->opcode);
        ASSERT_EQ(CoE::SDO::information::ListType::BACKUP, *resp_list_type);
        // Should be empty since no objects have BACKUP access
    }
}

TEST_F(Response, SDO_info_OD_list_settings)
{
    CoE::SDO::information::ListType list_type = CoE::SDO::information::ListType::SETTINGS;
    std::vector<uint8_t> raw_message = createTestSDOInfoMessage(CoE::SDO::information::GET_OD_LIST_REQ, &list_type, sizeof(list_type));
    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));

    ASSERT_NE(nullptr, response_msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, response_msg->process());
    auto const& msg = mbx.readyToSend();

    {
        auto header = pointData<mailbox::Header>(msg.data());
        auto coe = pointData<CoE::Header>(header);
        auto sdo = pointData<CoE::ServiceDataInfo>(coe);
        auto resp_list_type = pointData<CoE::SDO::information::ListType>(sdo);

        ASSERT_EQ(CoE::Service::SDO_INFORMATION, coe->service);
        ASSERT_EQ(CoE::SDO::information::GET_OD_LIST_RESP, sdo->opcode);
        ASSERT_EQ(CoE::SDO::information::ListType::SETTINGS, *resp_list_type);
        // Should be empty since no objects have SETTINGS access
    }
}

TEST_F(Response, createSDOMessage_txpdo)
{
    std::vector<uint8_t> raw_message(TEST_MAILBOX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw_message.data());
    header->type = mailbox::Type::CoE;
    auto coe = pointData<CoE::Header>(header);
    coe->service = CoE::Service::TxPDO;

    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));
    ASSERT_EQ(nullptr, response_msg);
}

TEST_F(Response, createSDOMessage_rxpdo)
{
    std::vector<uint8_t> raw_message(TEST_MAILBOX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw_message.data());
    header->type = mailbox::Type::CoE;
    auto coe = pointData<CoE::Header>(header);
    coe->service = CoE::Service::RxPDO;

    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));
    ASSERT_EQ(nullptr, response_msg);
}

TEST_F(Response, createSDOMessage_txpdo_remote_request)
{
    std::vector<uint8_t> raw_message(TEST_MAILBOX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw_message.data());
    header->type = mailbox::Type::CoE;
    auto coe = pointData<CoE::Header>(header);
    coe->service = CoE::Service::TxPDO_REMOTE_REQUEST;

    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));
    ASSERT_EQ(nullptr, response_msg);
}

TEST_F(Response, createSDOMessage_rxpdo_remote_request)
{
    std::vector<uint8_t> raw_message(TEST_MAILBOX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw_message.data());
    header->type = mailbox::Type::CoE;
    auto coe = pointData<CoE::Header>(header);
    coe->service = CoE::Service::RxPDO_REMOTE_REQUEST;

    auto response_msg = createSDOMessage(&mbx, std::move(raw_message));
    ASSERT_EQ(nullptr, response_msg);
}
