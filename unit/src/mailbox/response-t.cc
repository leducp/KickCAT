#include <gtest/gtest.h>

#include "mocks/ESC.h"

#include "kickcat/Mailbox.h"
#include "kickcat/CoE/mailbox/request.h"
#include "kickcat/CoE/mailbox/response.h"

using namespace kickcat;
using namespace kickcat::mailbox::response;

std::vector<uint8_t> createTestReadSDO(uint16_t index, uint8_t subindex, bool CA=false)
{
    uint32_t data;
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage msg{256, index, subindex, CA, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};

    std::vector<uint8_t> raw_message;
    raw_message.insert(raw_message.begin(), msg.data(), msg.data() + 256);
    return raw_message;
}

std::vector<uint8_t> createTestWriteSDO(uint16_t index, uint8_t subindex, uint32_t data, bool CA=false)
{
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage msg{256, index, subindex, CA, CoE::SDO::request::DOWNLOAD, &data, &data_size, 1ms};

    std::vector<uint8_t> raw_message;
    raw_message.insert(raw_message.begin(), msg.data(), msg.data() + 256);
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

class Response : public ::testing::Test
{
public:
    void SetUp() override
    {
        mbx.enableCoE(std::move(createTestDictionary()));
    }

    kickcat::MockESC esc;
    Mailbox mbx{&esc, 256, 1};
};

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
        mailbox::request::SDOMessage msg{256, 0x7000, 1, true, CoE::SDO::request::DOWNLOAD, &data, &data_size, 1ms};

        raw_message.insert(raw_message.begin(), msg.data(), msg.data() + 256);
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
