#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>

#include "kickcat/Mailbox.h"
#include "kickcat/CoE/mailbox/request.h"
#include "kickcat/FoE/mailbox/request.h"

using namespace kickcat;
using namespace kickcat::mailbox::request;

namespace
{
    constexpr uint16_t MBX_SIZE = 64;
    constexpr uint16_t CAPACITY = MBX_SIZE - FoE::OVERHEAD;

    struct PDU
    {
        uint8_t opcode;
        uint32_t value;
        std::vector<uint8_t> payload;
    };

    PDU decode(uint8_t const* raw)
    {
        auto const* header = pointData<mailbox::Header>(raw);
        auto const* foe = pointData<FoE::Header>(header);
        PDU pdu;
        pdu.opcode = foe->opcode;
        pdu.value = foe->value;
        uint8_t const* payload = pointData<uint8_t>(foe);
        pdu.payload.assign(payload, payload + header->len - sizeof(FoE::Header));
        return pdu;
    }

    std::vector<uint8_t> pattern(std::size_t size)
    {
        std::vector<uint8_t> data(size);
        std::iota(data.begin(), data.end(), uint8_t{0});
        return data;
    }
}

class FoE_Request : public ::testing::Test
{
public:
    void SetUp() override
    {
        mailbox.recv_size = MBX_SIZE;
        mailbox.send_size = MBX_SIZE;
    }

    // Prepare the next slave reply in raw_message
    uint8_t const* reply(uint8_t opcode, uint32_t value, std::vector<uint8_t> const& payload = {})
    {
        std::memset(raw_message, 0, sizeof(raw_message));
        auto* header = pointData<mailbox::Header>(raw_message);
        auto* foe = pointData<FoE::Header>(header);
        header->type = mailbox::Type::FoE;
        header->len  = static_cast<uint16_t>(sizeof(FoE::Header) + payload.size());
        foe->opcode  = opcode;
        foe->value   = value;
        std::copy(payload.begin(), payload.end(), pointData<uint8_t>(foe));
        return raw_message;
    }

    // Deliver a slave reply, then take the next PDU the master sends
    PDU exchange(uint8_t opcode, uint32_t value, std::vector<uint8_t> const& payload = {})
    {
        EXPECT_TRUE(mailbox.receive(reply(opcode, value, payload)));
        EXPECT_EQ(1, mailbox.to_send.size());
        return decode(mailbox.send()->data());
    }

protected:
    Mailbox mailbox;
    uint8_t raw_message[MBX_SIZE];
};


TEST_F(FoE_Request, inactive)
{
    mailbox.recv_size = 0;
    ASSERT_THROW(mailbox.createFoERead("file", 0), Error);
    ASSERT_THROW(mailbox.createFoEWrite("file", 0, {}), Error);
}

TEST_F(FoE_Request, invalid_name)
{
    ASSERT_THROW(mailbox.createFoERead("", 0), Error);
    ASSERT_THROW(mailbox.createFoERead(std::string(MBX_SIZE - FoE::OVERHEAD + 1, 'a'), 0), Error);
    ASSERT_NO_THROW(mailbox.createFoERead(std::string(MBX_SIZE - FoE::OVERHEAD, 'a'), 0));
}

TEST_F(FoE_Request, read_request)
{
    mailbox.createFoERead("fw.bin", 0xCAFEDECA);
    auto msg = mailbox.send();

    auto const* header = pointData<mailbox::Header>(msg->data());
    ASSERT_EQ(mailbox::Type::FoE, header->type);
    ASSERT_EQ(MBX_SIZE, msg->size());

    PDU pdu = decode(msg->data());
    ASSERT_EQ(FoE::opcode::READ, pdu.opcode);
    ASSERT_EQ(0xCAFEDECA, pdu.value);
    ASSERT_EQ("fw.bin", std::string(pdu.payload.begin(), pdu.payload.end()));
    ASSERT_EQ(MessageStatus::RUNNING, msg->status());
}

TEST_F(FoE_Request, read_multi_packet)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();

    auto file = pattern(CAPACITY + 10);
    std::vector<uint8_t> first(file.begin(), file.begin() + CAPACITY);
    std::vector<uint8_t> second(file.begin() + CAPACITY, file.end());

    PDU ack = exchange(FoE::opcode::DATA, 1, first);
    ASSERT_EQ(FoE::opcode::ACK, ack.opcode);
    ASSERT_EQ(1, ack.value);
    ASSERT_EQ(MessageStatus::RUNNING, foe->status());
    ASSERT_EQ(CAPACITY, foe->bytesTransferred());

    // The final ACK is still to be sent: the transfer is not over yet
    ASSERT_TRUE(mailbox.receive(reply(FoE::opcode::DATA, 2, second)));
    ASSERT_EQ(MessageStatus::RUNNING, foe->status());

    ack = decode(mailbox.send()->data());
    ASSERT_EQ(FoE::opcode::ACK, ack.opcode);
    ASSERT_EQ(2, ack.value);
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
    ASSERT_TRUE(mailbox.to_process.empty());
    ASSERT_EQ(file, foe->file());
}

TEST_F(FoE_Request, read_exact_multiple)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();

    auto file = pattern(CAPACITY);
    exchange(FoE::opcode::DATA, 1, file);
    ASSERT_EQ(MessageStatus::RUNNING, foe->status());

    PDU ack = exchange(FoE::opcode::DATA, 2);
    ASSERT_EQ(2, ack.value);
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
    ASSERT_EQ(file, foe->file());
}

TEST_F(FoE_Request, read_busy)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();

    PDU ack = exchange(FoE::opcode::BUSY, 0);
    ASSERT_EQ(FoE::opcode::ACK, ack.opcode);
    ASSERT_EQ(0, ack.value);

    exchange(FoE::opcode::DATA, 1, pattern(CAPACITY));
    ack = exchange(FoE::opcode::BUSY, 0);
    ASSERT_EQ(1, ack.value);

    exchange(FoE::opcode::DATA, 2, pattern(3));
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
    ASSERT_EQ(CAPACITY + 3, foe->file().size());
}

TEST_F(FoE_Request, read_wrong_packet_number)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();

    PDU error = exchange(FoE::opcode::DATA, 2, pattern(3));
    ASSERT_EQ(FoE::opcode::ERROR, error.opcode);
    ASSERT_EQ(FoE::result::PACKET_NUMBER_WRONG, error.value);
    ASSERT_EQ(MessageStatus::FOE_PACKET_NUMBER_WRONG, foe->status());
    ASSERT_TRUE(mailbox.to_process.empty());
}

TEST_F(FoE_Request, read_unexpected_opcode)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();

    PDU error = exchange(FoE::opcode::ACK, 0);
    ASSERT_EQ(FoE::opcode::ERROR, error.opcode);
    ASSERT_EQ(FoE::result::ILLEGAL, error.value);
    ASSERT_EQ(MessageStatus::FOE_UNEXPECTED_OPCODE, foe->status());
}

TEST_F(FoE_Request, slave_error)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();

    std::string text = "no such file";
    ASSERT_TRUE(mailbox.receive(reply(FoE::opcode::ERROR, FoE::result::NOT_FOUND, {text.begin(), text.end()})));
    ASSERT_EQ(FoE::result::NOT_FOUND, foe->status());
    ASSERT_EQ(text, foe->errorText());
    ASSERT_TRUE(mailbox.to_process.empty());
    ASSERT_TRUE(mailbox.to_send.empty());
}

TEST_F(FoE_Request, slave_error_tftp_code)
{
    auto foe = mailbox.createFoEWrite("fw.bin", 0, pattern(4));
    mailbox.send();

    ASSERT_TRUE(mailbox.receive(reply(FoE::opcode::ERROR, 0x2)));
    ASSERT_EQ(FoE::result::ACCESS_DENIED, foe->status());
}

TEST_F(FoE_Request, invalid_reply)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();

    reply(FoE::opcode::DATA, 1);
    pointData<mailbox::Header>(raw_message)->len = 2;
    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::FOE_INVALID_REPLY, foe->status());

    foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();
    reply(FoE::opcode::DATA, 1);
    pointData<mailbox::Header>(raw_message)->len = MBX_SIZE;
    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::FOE_INVALID_REPLY, foe->status());
}

TEST_F(FoE_Request, unrelated_reply)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();

    reply(FoE::opcode::DATA, 1);
    pointData<mailbox::Header>(raw_message)->type = mailbox::Type::CoE;
    ASSERT_FALSE(mailbox.receive(raw_message));

    reply(FoE::opcode::DATA, 1);
    pointData<mailbox::Header>(raw_message)->address = mailbox::GATEWAY_MESSAGE_MASK;
    ASSERT_FALSE(mailbox.receive(raw_message));

    ASSERT_EQ(MessageStatus::RUNNING, foe->status());
}

TEST_F(FoE_Request, write)
{
    auto file = pattern(CAPACITY + 10);
    auto foe = mailbox.createFoEWrite("fw.bin", 0x42, file);
    PDU request = decode(mailbox.send()->data());
    ASSERT_EQ(FoE::opcode::WRITE, request.opcode);
    ASSERT_EQ(0x42, request.value);
    ASSERT_EQ("fw.bin", std::string(request.payload.begin(), request.payload.end()));

    PDU data = exchange(FoE::opcode::ACK, 0);
    ASSERT_EQ(FoE::opcode::DATA, data.opcode);
    ASSERT_EQ(1, data.value);
    ASSERT_EQ(std::vector<uint8_t>(file.begin(), file.begin() + CAPACITY), data.payload);
    ASSERT_EQ(0, foe->bytesTransferred());

    data = exchange(FoE::opcode::ACK, 1);
    ASSERT_EQ(2, data.value);
    ASSERT_EQ(std::vector<uint8_t>(file.begin() + CAPACITY, file.end()), data.payload);
    ASSERT_EQ(CAPACITY, foe->bytesTransferred());

    ASSERT_TRUE(mailbox.receive(reply(FoE::opcode::ACK, 2)));
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
    ASSERT_EQ(file.size(), foe->bytesTransferred());
    ASSERT_TRUE(mailbox.to_send.empty());
    ASSERT_TRUE(mailbox.to_process.empty());
}

TEST_F(FoE_Request, write_exact_multiple)
{
    auto foe = mailbox.createFoEWrite("fw.bin", 0, pattern(2 * CAPACITY));
    mailbox.send();

    ASSERT_EQ(CAPACITY, exchange(FoE::opcode::ACK, 0).payload.size());
    ASSERT_EQ(CAPACITY, exchange(FoE::opcode::ACK, 1).payload.size());
    PDU last = exchange(FoE::opcode::ACK, 2);
    ASSERT_EQ(3, last.value);
    ASSERT_TRUE(last.payload.empty());

    ASSERT_TRUE(mailbox.receive(reply(FoE::opcode::ACK, 3)));
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
}

TEST_F(FoE_Request, write_empty_file)
{
    auto foe = mailbox.createFoEWrite("fw.bin", 0, {});
    mailbox.send();

    PDU data = exchange(FoE::opcode::ACK, 0);
    ASSERT_EQ(FoE::opcode::DATA, data.opcode);
    ASSERT_EQ(1, data.value);
    ASSERT_TRUE(data.payload.empty());

    ASSERT_TRUE(mailbox.receive(reply(FoE::opcode::ACK, 1)));
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
}

TEST_F(FoE_Request, write_busy)
{
    auto foe = mailbox.createFoEWrite("fw.bin", 0, pattern(CAPACITY + 1));
    mailbox.send();

    PDU data = exchange(FoE::opcode::ACK, 0);
    PDU repeat = exchange(FoE::opcode::BUSY, 0);
    ASSERT_EQ(FoE::opcode::DATA, repeat.opcode);
    ASSERT_EQ(data.value, repeat.value);
    ASSERT_EQ(data.payload, repeat.payload);

    data = exchange(FoE::opcode::ACK, 1);
    ASSERT_EQ(2, data.value);
    ASSERT_TRUE(mailbox.receive(reply(FoE::opcode::ACK, 2)));
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
}

TEST_F(FoE_Request, write_wrong_ack)
{
    auto foe = mailbox.createFoEWrite("fw.bin", 0, pattern(CAPACITY + 1));
    mailbox.send();
    exchange(FoE::opcode::ACK, 0);

    PDU error = exchange(FoE::opcode::ACK, 0);
    ASSERT_EQ(FoE::opcode::ERROR, error.opcode);
    ASSERT_EQ(FoE::result::PACKET_NUMBER_WRONG, error.value);
    ASSERT_EQ(MessageStatus::FOE_PACKET_NUMBER_WRONG, foe->status());
}

TEST_F(FoE_Request, cancel_while_waiting_for_reply)
{
    auto foe = mailbox.createFoEWrite("fw.bin", 0, pattern(3 * CAPACITY));
    mailbox.send();
    exchange(FoE::opcode::ACK, 0);

    foe->cancel();
    ASSERT_EQ(MessageStatus::RUNNING, foe->status());

    PDU error = exchange(FoE::opcode::ACK, 1);
    ASSERT_EQ(FoE::opcode::ERROR, error.opcode);
    ASSERT_EQ(FoE::result::NOT_DEFINED, error.value);
    ASSERT_EQ(MessageStatus::FOE_CANCELLED, foe->status());
    ASSERT_TRUE(mailbox.to_process.empty());
}

TEST_F(FoE_Request, cancel_while_queued)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();
    ASSERT_TRUE(mailbox.receive(reply(FoE::opcode::DATA, 1, pattern(CAPACITY))));

    // The ACK is queued: the error goes out in its place
    foe->cancel();
    PDU error = decode(mailbox.send()->data());
    ASSERT_EQ(FoE::opcode::ERROR, error.opcode);
    ASSERT_EQ(MessageStatus::FOE_CANCELLED, foe->status());
    ASSERT_TRUE(mailbox.to_process.empty());
}

TEST_F(FoE_Request, cancel_after_the_end_has_no_effect)
{
    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();
    exchange(FoE::opcode::DATA, 1, pattern(3));
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());

    foe->cancel();
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
}

TEST_F(FoE_Request, timeout_applies_to_each_exchange)
{
    nanoseconds start = now();
    auto foe = mailbox.createFoERead("fw.bin", 0, 100ms);
    mailbox.send();

    for (int i = 0; i < 50; ++i)
    {
        now(); // the test clock advances by 1ms per call
    }
    exchange(FoE::opcode::DATA, 1, pattern(CAPACITY));

    ASSERT_EQ(MessageStatus::RUNNING, foe->status(start + 120ms));
    ASSERT_EQ(MessageStatus::TIMEDOUT, foe->status(start + 200ms));
}

TEST_F(FoE_Request, reply_reaches_transfer_not_check_message)
{
    // Bus::init seeds a CheckMessage ahead of any user message: it shall not swallow FoE replies.
    mailbox.to_process.push_back(std::make_shared<CheckMessage>(mailbox));

    auto foe = mailbox.createFoERead("fw.bin", 0);
    mailbox.send();

    exchange(FoE::opcode::DATA, 1, pattern(3));
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
    ASSERT_EQ(3, foe->file().size());
}

TEST(FoE_Protocol, normalize_error)
{
    ASSERT_EQ(FoE::result::NOT_DEFINED,       FoE::normalizeError(0x0000));
    ASSERT_EQ(FoE::result::FILE_INCOMPATIBLE, FoE::normalizeError(0x0012));
    ASSERT_EQ(0x0013,                         FoE::normalizeError(0x0013));
    ASSERT_EQ(FoE::result::NOT_FOUND,         FoE::normalizeError(FoE::result::NOT_FOUND));
}

TEST(FoE_Protocol, error_to_string)
{
    ASSERT_STREQ("Not found",                     FoE::errorToString(FoE::result::NOT_FOUND));
    ASSERT_STREQ("Unexpected FoE packet number",  FoE::errorToString(MessageStatus::FOE_PACKET_NUMBER_WRONG));
    ASSERT_STREQ("Unknown FoE error",             FoE::errorToString(0x1234));
}
