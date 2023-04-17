#include <gtest/gtest.h>
#include "kickcat/Mailbox.h"

using namespace kickcat;

class MailboxTest : public ::testing::Test
{
public:
    void SetUp() override
    {
        mailbox.recv_size = 256;
        mailbox.send_size = 256;

        header  = pointData<mailbox::Header>(raw_message);
        coe     = pointData<CoE::Header>(header);
        sdo     = pointData<CoE::ServiceData>(coe);
        emg     = pointData<CoE::Emergency>(coe);
        payload = pointData<void>(sdo);

        // Default address is 0 (local processing)
        header->address = 0;
    }

protected:
    Mailbox mailbox;
    uint8_t raw_message[256];

    // pointers on raw_message to prepare test payload
    mailbox::Header* header;
    CoE::Header* coe;
    CoE::Emergency* emg;
    CoE::ServiceData* sdo;
    void* payload;
};


TEST_F(MailboxTest, SyncManager_configuration)
{
    mailbox.send_size = 17;
    mailbox.send_offset = 8;
    mailbox.recv_size = 42;
    mailbox.recv_offset = 0x300;

    SyncManager SM[2];
    mailbox.generateSMConfig(SM);

    ASSERT_EQ(42,       SM[0].length);
    ASSERT_EQ(0x300,    SM[0].start_address);
    ASSERT_EQ(1,        SM[0].activate);
    ASSERT_EQ(0x26,     SM[0].control);

    ASSERT_EQ(17,       SM[1].length);
    ASSERT_EQ(8,        SM[1].start_address);
    ASSERT_EQ(1,        SM[1].activate);
    ASSERT_EQ(0x22,     SM[1].control);
}

TEST_F(MailboxTest, counter)
{
    for (int i = mailbox.nextCounter(); i < 100; ++i)
    {
        ASSERT_EQ(i % 7 + 1, mailbox.nextCounter());
    }
}

TEST_F(MailboxTest, received_unknown_message)
{
    ASSERT_FALSE(mailbox.receive(raw_message));
}

TEST_F(MailboxTest, received_emergency_message)
{
    // create reception callback
    auto emg_callback = std::make_shared<EmergencyMessage>(mailbox);
    mailbox.to_process.push_back(emg_callback);

    // raw data that represent an emergency message
    header->type = mailbox::Type::CoE;
    coe->service = CoE::Service::EMERGENCY;
    emg->error_code = 0x3310;

    ASSERT_EQ(0, mailbox.emergencies.size());
    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(1, mailbox.emergencies.size());
    ASSERT_EQ(0x3310,  mailbox.emergencies.at(0).error_code);
}

TEST_F(MailboxTest, emergency_callback_not_related_message)
{
    // create reception callback
    auto emg_callback = std::make_shared<EmergencyMessage>(mailbox);
    mailbox.to_process.push_back(emg_callback);

    header->type = mailbox::Type::CoE;
    coe->service = CoE::Service::SDO_INFORMATION;
    ASSERT_FALSE(mailbox.receive(raw_message));
    ASSERT_EQ(0, mailbox.emergencies.size());

    header->type = mailbox::Type::VoE;
    coe->service = CoE::Service::EMERGENCY;
    ASSERT_FALSE(mailbox.receive(raw_message));
    ASSERT_EQ(0, mailbox.emergencies.size());
}

TEST_F(MailboxTest, SDO_inactive_mailbox)
{
    mailbox.recv_size = 0;
    int32_t data = 0;
    uint32_t data_size = sizeof(data);
    ASSERT_THROW(mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size), kickcat::Error);
}

TEST_F(MailboxTest, SDO_upload_expedited_OK)
{
    int32_t data = 0;
    uint32_t data_size = sizeof(data);
    mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size);

    auto message = mailbox.send();
    ASSERT_EQ(MessageStatus::RUNNING, message->status());
    ASSERT_EQ(mailbox.recv_size, message->size());

    // check message content
    auto const* mbx_section = pointData<mailbox::Header>(message->data());
    auto const* coe_section = pointData<CoE::Header>(mbx_section);
    auto const* sdo_section = pointData<CoE::ServiceData>(coe_section);

    ASSERT_EQ(mailbox::Type::CoE,           mbx_section->type);
    ASSERT_EQ(CoE::Service::SDO_REQUEST,    coe_section->service);
    ASSERT_EQ(CoE::SDO::request::UPLOAD,    sdo_section->command);
    ASSERT_EQ(0x1018,                       sdo_section->index);
    ASSERT_EQ(1,                            sdo_section->subindex);
    ASSERT_EQ(false,                        sdo_section->complete_access);

    // reply
    header->address = 0;
    header->type = mailbox::Type::CoE;
    coe->service = CoE::Service::SDO_RESPONSE;
    sdo->transfer_type = 1;
    sdo->block_size = 0; // 4 bytes
    sdo->command = CoE::SDO::request::UPLOAD;
    sdo->index = 0x1018;
    sdo->subindex = 1;
    *static_cast<int32_t*>(payload) = 0xCAFEDECA;
    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(0xCAFEDECA, data);
}


TEST_F(MailboxTest, SDO_upload_standard_OK)
{
    int32_t data[4] = {0};
    uint32_t data_size = sizeof(data);
    mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size);

    auto message = mailbox.send();
    ASSERT_EQ(MessageStatus::RUNNING, message->status());
    ASSERT_EQ(mailbox.recv_size, message->size());

    // check message content
    auto const* mbx_section = pointData<mailbox::Header>(message->data());
    auto const* coe_section = pointData<CoE::Header>(mbx_section);
    auto const* sdo_section = pointData<CoE::ServiceData>(coe_section);

    ASSERT_EQ(mailbox::Type::CoE,           mbx_section->type);
    ASSERT_EQ(CoE::Service::SDO_REQUEST,    coe_section->service);
    ASSERT_EQ(CoE::SDO::request::UPLOAD,    sdo_section->command);
    ASSERT_EQ(0x1018,                       sdo_section->index);
    ASSERT_EQ(1,                            sdo_section->subindex);
    ASSERT_EQ(false,                        sdo_section->complete_access);

    // reply
    header->type = mailbox::Type::CoE;
    coe->service = CoE::Service::SDO_RESPONSE;
    header->len = 10 + 8;
    sdo->transfer_type = 0;
    sdo->block_size = 0; // 4 bytes
    sdo->command = CoE::SDO::request::UPLOAD;
    sdo->index = 0x1018;
    sdo->subindex = 1;
    int32_t* reply = static_cast<int32_t*>(payload);
    reply[0] = 8; // payload size
    reply[1] = 0xDEADBEEF;
    reply[2] = 0xA5A5A5A5;
    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(0xDEADBEEF, data[0]);
    ASSERT_EQ(0xA5A5A5A5, data[1]);
    ASSERT_EQ(8, data_size);
}


TEST_F(MailboxTest, SDO_upload_segmented_OK)
{
    int32_t data[4] = {0};
    uint32_t data_size = sizeof(data);
    mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size);

    auto message = mailbox.send();
    ASSERT_EQ(MessageStatus::RUNNING, message->status());
    ASSERT_EQ(mailbox.recv_size, message->size());

    // check message content
    auto const* mbx_section = pointData<mailbox::Header>(message->data());
    auto const* coe_section = pointData<CoE::Header>(mbx_section);
    auto const* sdo_section = pointData<CoE::ServiceData>(coe_section);

    ASSERT_EQ(mailbox::Type::CoE,           mbx_section->type);
    ASSERT_EQ(CoE::Service::SDO_REQUEST,    coe_section->service);
    ASSERT_EQ(CoE::SDO::request::UPLOAD,    sdo_section->command);
    ASSERT_EQ(0x1018,                       sdo_section->index);
    ASSERT_EQ(1,                            sdo_section->subindex);
    ASSERT_EQ(false,                        sdo_section->complete_access);

    // reply
    header->type = mailbox::Type::CoE;
    header->len = 10 + 8;
    coe->service = CoE::Service::SDO_RESPONSE;
    sdo->transfer_type = 0;
    sdo->block_size = 0; // 4 bytes
    sdo->command = CoE::SDO::request::UPLOAD;
    sdo->index = 0x1018;
    sdo->subindex = 1;
    int32_t* reply = static_cast<int32_t*>(payload);
    reply[0] = 16; // complete size - partial (seggmented) since more than contains in ths header
    reply[1] = 8;  // segment size
    reply[2] = 0xDEADBEEF;
    reply[3] = 0xA5A5A5A5;
    ASSERT_TRUE(mailbox.receive(raw_message));
    message = mailbox.send();
    ASSERT_EQ(MessageStatus::RUNNING, message->status());
    ASSERT_EQ(CoE::SDO::request::UPLOAD_SEGMENTED, sdo_section->command);

    header->len = 10 + 8;
    sdo->size_indicator = 0;
    sdo->complete_access = not sdo->complete_access;
    sdo->command = CoE::SDO::response::UPLOAD_SEGMENTED;
    reply[0] = 8;
    reply[1] = 0xCAFEDECA;
    reply[2] = 0xD0D0FACE;
    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::SUCCESS, message->status());

    ASSERT_EQ(0xDEADBEEF, data[0]);
    ASSERT_EQ(0xA5A5A5A5, data[1]);
    ASSERT_EQ(0xCAFEDECA, data[2]);
    ASSERT_EQ(0xD0D0FACE, data[3]);
    ASSERT_EQ(16, data_size);
}



TEST_F(MailboxTest, SDO_download_OK)
{
    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::DOWNLOAD, &data, &data_size);

    auto message = mailbox.send();
    ASSERT_EQ(MessageStatus::RUNNING, message->status());

    // check message content
    auto const* mbx_section = pointData<mailbox::Header>(message->data());
    auto const* coe_section = pointData<CoE::Header>(mbx_section);
    auto const* sdo_section = pointData<CoE::ServiceData>(coe_section);
    auto const* sdo_payload = pointData<uint32_t>(sdo_section);

    ASSERT_EQ(mailbox::Type::CoE,           mbx_section->type);
    ASSERT_EQ(CoE::Service::SDO_REQUEST,    coe_section->service);
    ASSERT_EQ(CoE::SDO::request::DOWNLOAD,  sdo_section->command);
    ASSERT_EQ(0x1018,                       sdo_section->index);
    ASSERT_EQ(1,                            sdo_section->subindex);
    ASSERT_EQ(false,                        sdo_section->complete_access);
    ASSERT_EQ(0,                            sdo_section->block_size);
    ASSERT_EQ(0xCAFEDECA,                   *sdo_payload);

    // reply
    header->type = mailbox::Type::CoE;
    coe->service = CoE::Service::SDO_RESPONSE;
    sdo->command = CoE::SDO::request::DOWNLOAD;
    sdo->index = 0x1018;
    sdo->subindex = 1;
    ASSERT_TRUE(mailbox.receive(raw_message));
}

TEST_F(MailboxTest, SDO_download_abort)
{
    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::DOWNLOAD, &data, &data_size);

    auto message = mailbox.send();
    ASSERT_EQ(MessageStatus::RUNNING, message->status());

    // check message content
    auto const* mbx_section = pointData<mailbox::Header>(message->data());
    auto const* coe_section = pointData<CoE::Header>(mbx_section);
    auto const* sdo_section = pointData<CoE::ServiceData>(coe_section);
    auto const* sdo_payload = pointData<uint32_t>(sdo_section);

    ASSERT_EQ(mailbox::Type::CoE,           mbx_section->type);
    ASSERT_EQ(CoE::Service::SDO_REQUEST,    coe_section->service);
    ASSERT_EQ(CoE::SDO::request::DOWNLOAD,  sdo_section->command);
    ASSERT_EQ(0x1018,                       sdo_section->index);
    ASSERT_EQ(1,                            sdo_section->subindex);
    ASSERT_EQ(false,                        sdo_section->complete_access);
    ASSERT_EQ(0,                            sdo_section->block_size);
    ASSERT_EQ(0xCAFEDECA,                   *sdo_payload);

    // reply
    header->type = mailbox::Type::CoE;
    coe->service = CoE::Service::SDO_RESPONSE;
    sdo->command = CoE::SDO::request::ABORT;
    sdo->index = 0x1018;
    sdo->subindex = 1;
    *static_cast<int32_t*>(payload) = 0x06010000;
    ASSERT_TRUE(mailbox.receive(raw_message));

    ASSERT_EQ(0x06010000, message->status());
}

TEST_F(MailboxTest, SDO_timedout)
{
    nanoseconds const TIMEOUT = 10ms;
    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::DOWNLOAD, &data, &data_size, TIMEOUT);

    auto message = mailbox.send();

    // unrelated reply to process the message
    header->type = mailbox::Type::VoE;

    nanoseconds now = since_epoch();

    ASSERT_FALSE(mailbox.receive(raw_message, now + 1ms)); // unrelated message -> false
    ASSERT_EQ(MessageStatus::RUNNING, message->status(now + 1ms));
    ASSERT_EQ(1, mailbox.to_process.size());

    ASSERT_FALSE(mailbox.receive(raw_message, now + TIMEOUT)); // unrelated message -> false
    ASSERT_EQ(MessageStatus::TIMEDOUT, message->status(now + TIMEOUT));
    ASSERT_EQ(0, mailbox.to_process.size());
}


TEST_F(MailboxTest, gateway_message)
{
    constexpr uint16_t GATEWAY_INDEX = 42;
    // Create a standard SDO with a non local address
    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size);
    auto msg = mailbox.send();
    msg->setAddress(1001);

    // Convert the non local SDO to a local gteway message
    auto gw_msg = mailbox.createGatewayMessage(msg->data(), msg->size(), GATEWAY_INDEX);

    ASSERT_EQ(GATEWAY_INDEX, gw_msg->gatewayIndex());
    ASSERT_EQ(GATEWAY_INDEX | mailbox::GATEWAY_MESSAGE_MASK, gw_msg->address());

    // Receive the gateway message - shall not be processed since non local and gw_msg not send for the moment
    ASSERT_FALSE(mailbox.receive(gw_msg->data()));

    // Receive the gateway message - OK cause send(), address shall be back to 1001
    mailbox.send();
    ASSERT_TRUE(mailbox.receive(gw_msg->data()));
    ASSERT_EQ(1001, gw_msg->address());
}
