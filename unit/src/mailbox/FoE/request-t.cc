#include <gtest/gtest.h>

#include <cstring>
#include <numeric>

#include "kickcat/Mailbox.h"
#include "kickcat/FoE/mailbox/request.h"

using namespace kickcat;
using namespace kickcat::mailbox::request;

class FoE_Request : public ::testing::Test
{
public:
    void SetUp() override
    {
        std::memset(raw_message, 0, sizeof(raw_message));
        mailbox.recv_size = 32;
        mailbox.send_size = 32;

        header = pointData<mailbox::Header>(raw_message);
        foe    = pointData<FoE::Header>(header);

        header->address = 0;
        header->type    = mailbox::Type::FoE;
    }

    // build a slave DATA reply into raw_message
    void makeData(uint32_t packet_number, uint8_t const* data, uint16_t len)
    {
        foe->opcode = FoE::opcode::DATA;
        auto* dh = pointData<FoE::data::Header>(foe);
        dh->packet_number = packet_number;
        std::memcpy(pointData<uint8_t>(dh), data, len);
        header->len = static_cast<uint16_t>(sizeof(FoE::Header) + sizeof(FoE::data::Header) + len);
    }

protected:
    Mailbox mailbox;
    uint8_t raw_message[256];
    mailbox::Header* header;
    FoE::Header*     foe;
};

TEST_F(FoE_Request, inactive)
{
    mailbox.recv_size = 0;
    uint32_t size = 8;
    uint8_t buf[8] = {0};
    ASSERT_THROW(mailbox.createFoERead("f", 0, buf, &size), Error);
    ASSERT_THROW(mailbox.createFoEWrite("f", 0, buf, sizeof(buf)), Error);
}

TEST_F(FoE_Request, read_builds_request)
{
    uint32_t size = 16;
    uint8_t buf[16] = {0};
    mailbox.createFoERead("test.bin", 0xdeadbeef, buf, &size);
    auto msg = mailbox.send();
    ASSERT_EQ(MessageStatus::RUNNING, msg->status());

    auto const* h  = pointData<mailbox::Header>(msg->data());
    auto const* f  = pointData<FoE::Header>(h);
    auto const* rd = pointData<FoE::read::Header>(f);
    ASSERT_EQ(mailbox::Type::FoE, h->type);
    ASSERT_EQ(FoE::opcode::READ, f->opcode);
    ASSERT_EQ(0xdeadbeef, rd->password);
    std::string name(reinterpret_cast<char const*>(pointData<uint8_t>(rd)), h->len - sizeof(FoE::Header) - sizeof(FoE::read::Header));
    ASSERT_EQ("test.bin", name);
}

TEST_F(FoE_Request, read_single_packet)
{
    uint32_t size = 32;
    uint8_t buf[32] = {0};
    mailbox.createFoERead("f", 0, buf, &size);
    auto msg = mailbox.send();

    uint8_t payload[10];
    std::iota(payload, payload + 10, 1);
    makeData(1, payload, 10); // 10 < max (send_size-12=20) => last packet

    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::SUCCESS, msg->status());
    ASSERT_EQ(10u, size);
    ASSERT_EQ(0, std::memcmp(buf, payload, 10));

    // the master queued a final Ack(1) that it sends fire-and-forget
    auto ack = mailbox.send();
    auto const* af = pointData<FoE::Header>(pointData<mailbox::Header>(ack->data()));
    ASSERT_EQ(FoE::opcode::ACK, af->opcode);
    ASSERT_EQ(1u, pointData<FoE::ack::Header>(af)->packet_number);
}

TEST_F(FoE_Request, read_error_is_surfaced)
{
    uint32_t size = 16;
    uint8_t buf[16] = {0};
    mailbox.createFoERead("missing", 0, buf, &size);
    auto msg = mailbox.send();

    foe->opcode = FoE::opcode::ERROR;
    pointData<FoE::error::Header>(foe)->error_code = FoE::result::NOT_FOUND;
    header->len = sizeof(FoE::Header) + sizeof(FoE::error::Header);

    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::FOE_RESULT | FoE::result::NOT_FOUND, msg->status());
    ASSERT_EQ(FoE::result::NOT_FOUND, msg->status() & 0xFFFF);
}

TEST_F(FoE_Request, read_wrong_packet_number)
{
    uint32_t size = 32;
    uint8_t buf[32] = {0};
    mailbox.createFoERead("f", 0, buf, &size);
    auto msg = mailbox.send();

    uint8_t payload[4] = {1, 2, 3, 4};
    makeData(2, payload, 4); // expected 1
    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::FOE_PACKET_NUMBER, msg->status());
}

TEST_F(FoE_Request, read_buffer_too_small)
{
    uint32_t size = 4;
    uint8_t buf[4] = {0};
    mailbox.createFoERead("f", 0, buf, &size);
    auto msg = mailbox.send();

    uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    makeData(1, payload, 8); // 8 > buffer capacity 4
    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::FOE_CLIENT_BUFFER_TOO_SMALL, msg->status());
}

TEST_F(FoE_Request, wire_layout_data_packet_number_offset)
{
    // packet_number is a DWORD at offset 8 (6-byte mailbox header + 2-byte FoE header), little-endian.
    // Pins the field width independently of the reader (the scaffold had it as uint16_t).
    std::memset(raw_message, 0, sizeof(raw_message));
    header->type = mailbox::Type::FoE;
    foe->opcode  = FoE::opcode::DATA;
    pointData<FoE::data::Header>(foe)->packet_number = 0x04030201u;
    ASSERT_EQ(0x01, raw_message[8]);
    ASSERT_EQ(0x02, raw_message[9]);
    ASSERT_EQ(0x03, raw_message[10]);
    ASSERT_EQ(0x04, raw_message[11]);
}

TEST_F(FoE_Request, read_busy_retries)
{
    uint32_t size = 32;
    uint8_t buf[32] = {0};
    mailbox.createFoERead("f", 0, buf, &size);
    auto msg = mailbox.send();

    foe->opcode = FoE::opcode::BUSY;
    auto* bh = pointData<FoE::busy::Header>(foe);
    bh->done   = 0;
    bh->entire = 100;
    header->len = sizeof(FoE::Header) + sizeof(FoE::busy::Header);

    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::RUNNING, msg->status()); // re-queued, not finalized
    ASSERT_FALSE(mailbox.to_send.empty());            // the request is queued for re-send
}

TEST_F(FoE_Request, read_unexpected_opcode_is_noop)
{
    uint32_t size = 32;
    uint8_t buf[32] = {0};
    mailbox.createFoERead("f", 0, buf, &size);
    auto msg = mailbox.send();

    foe->opcode = FoE::opcode::ACK; // wrong opcode for a read transfer
    pointData<FoE::ack::Header>(foe)->packet_number = 0;
    header->len = sizeof(FoE::Header) + sizeof(FoE::ack::Header);

    ASSERT_FALSE(mailbox.receive(raw_message)); // not consumed
    ASSERT_EQ(MessageStatus::RUNNING, msg->status());
}

TEST_F(FoE_Request, write_wrong_ack_packet_number_fails)
{
    uint8_t file[5] = {1, 2, 3, 4, 5};
    mailbox.createFoEWrite("f.bin", 0, file, sizeof(file));
    auto msg = mailbox.send();

    foe->opcode = FoE::opcode::ACK;
    pointData<FoE::ack::Header>(foe)->packet_number = 99; // expected 0 (ack of the Write Request)
    header->len = sizeof(FoE::Header) + sizeof(FoE::ack::Header);

    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::FOE_PACKET_NUMBER, msg->status());
}

TEST_F(FoE_Request, write_builds_request_then_first_data)
{
    uint8_t file[5] = {9, 8, 7, 6, 5};
    mailbox.createFoEWrite("fw.bin", 0, file, sizeof(file));
    auto wrq = mailbox.send();

    auto const* wf = pointData<FoE::Header>(pointData<mailbox::Header>(wrq->data()));
    ASSERT_EQ(FoE::opcode::WRITE, wf->opcode);

    // slave acks the Write Request (packet 0): master must answer with Data(1)
    foe->opcode = FoE::opcode::ACK;
    pointData<FoE::ack::Header>(foe)->packet_number = 0;
    header->len = sizeof(FoE::Header) + sizeof(FoE::ack::Header);
    ASSERT_TRUE(mailbox.receive(raw_message));

    auto data = mailbox.send();
    auto const* h  = pointData<mailbox::Header>(data->data());
    auto const* df = pointData<FoE::Header>(h);
    ASSERT_EQ(FoE::opcode::DATA, df->opcode);
    ASSERT_EQ(1u, pointData<FoE::data::Header>(df)->packet_number);
    ASSERT_EQ(sizeof(FoE::Header) + sizeof(FoE::data::Header) + sizeof(file), h->len);
}
