#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <numeric>

#include "kickcat/Mailbox.h"
#include "kickcat/FoE/mailbox/request.h"
#include "kickcat/FoE/mailbox/response.h"
#include "kickcat/FoE/Storage.h"

using namespace kickcat;
using namespace kickcat::mailbox::response;
namespace MessageStatus = mailbox::request::MessageStatus;

namespace
{
    constexpr uint16_t MBX_SIZE = 32;
    constexpr uint16_t CAPACITY = MBX_SIZE - FoE::OVERHEAD;

    // Files in memory, with the transfer outcomes counted
    class MemoryStorage final : public FoE::AbstractStorage
    {
    public:
        class Reader final : public FoE::AbstractReader
        {
        public:
            Reader(MemoryStorage& storage, std::vector<uint8_t> content)
                : storage_{storage}
                , content_{std::move(content)}
            {
                ++storage_.open;
            }

            ~Reader() override
            {
                --storage_.open;
            }

            uint32_t read(uint8_t* buffer, uint32_t capacity, uint32_t& size) override
            {
                size = std::min<uint32_t>(capacity, static_cast<uint32_t>(content_.size() - offset_));
                std::copy_n(content_.data() + offset_, size, buffer);
                offset_ += size;
                return 0;
            }

        private:
            MemoryStorage& storage_;
            std::vector<uint8_t> content_;
            std::size_t offset_{0};
        };

        class Writer final : public FoE::AbstractWriter
        {
        public:
            Writer(MemoryStorage& storage, std::string name)
                : storage_{storage}
                , name_{std::move(name)}
            {
                ++storage_.open;
            }

            ~Writer() override
            {
                --storage_.open;
                if (not committed_)
                {
                    ++storage_.discarded;
                }
            }

            uint32_t write(uint8_t const* data, uint32_t size) override
            {
                content_.insert(content_.end(), data, data + size);
                return 0;
            }

            uint32_t commit() override
            {
                if (storage_.commit_error != 0)
                {
                    return storage_.commit_error;
                }
                storage_.files[name_] = content_;
                committed_ = true;
                ++storage_.commits;
                return 0;
            }

        private:
            MemoryStorage& storage_;
            std::string name_;
            std::vector<uint8_t> content_;
            bool committed_{false};
        };

        std::tuple<uint32_t, std::unique_ptr<FoE::AbstractReader>> openRead(std::string_view name, uint32_t password) override
        {
            if (password != expected_password)
            {
                return {FoE::result::NO_RIGHTS, nullptr};
            }
            auto it = files.find(std::string{name});
            if (it == files.end())
            {
                return {FoE::result::NOT_FOUND, nullptr};
            }
            return {0, std::make_unique<Reader>(*this, it->second)};
        }

        std::tuple<uint32_t, std::unique_ptr<FoE::AbstractWriter>> openWrite(std::string_view name, uint32_t password) override
        {
            if (password != expected_password)
            {
                return {FoE::result::NO_RIGHTS, nullptr};
            }
            return {0, std::make_unique<Writer>(*this, std::string{name})};
        }

        std::map<std::string, std::vector<uint8_t>> files;
        uint32_t expected_password{0};
        uint32_t commit_error{0};
        int open{0};        // readers and writers alive
        int commits{0};
        int discarded{0};   // writers released without a successful commit
    };

    std::vector<uint8_t> pattern(std::size_t size)
    {
        std::vector<uint8_t> data(size);
        std::iota(data.begin(), data.end(), uint8_t{7});
        return data;
    }

    std::vector<uint8_t> createPDU(uint8_t opcode, uint32_t value, std::vector<uint8_t> const& payload = {})
    {
        std::vector<uint8_t> raw(MBX_SIZE, 0);
        auto* header = pointData<mailbox::Header>(raw.data());
        auto* foe = pointData<FoE::Header>(header);
        header->type = mailbox::Type::FoE;
        header->len  = static_cast<uint16_t>(sizeof(FoE::Header) + payload.size());
        foe->opcode  = opcode;
        foe->value   = value;
        std::copy(payload.begin(), payload.end(), pointData<uint8_t>(foe));
        return raw;
    }

    std::vector<uint8_t> createRequest(uint8_t opcode, std::string const& name, uint32_t password = 0)
    {
        return createPDU(opcode, password, {name.begin(), name.end()});
    }

    struct PDU
    {
        uint8_t type;
        uint8_t opcode;
        uint32_t value;
        std::vector<uint8_t> payload;
    };

    PDU decode(std::vector<uint8_t> const& raw)
    {
        auto const* header = pointData<mailbox::Header>(raw.data());
        auto const* foe = pointData<FoE::Header>(header);
        PDU pdu;
        pdu.type = header->type;
        pdu.opcode = foe->opcode;
        pdu.value = foe->value;
        uint8_t const* payload = pointData<uint8_t>(foe);
        pdu.payload.assign(payload, payload + header->len - sizeof(FoE::Header));
        return pdu;
    }
}

class FoE_Response : public ::testing::Test
{
public:
    void SetUp() override
    {
        slave.enableFoE(storage);
        master.recv_size = MBX_SIZE;
        master.send_size = MBX_SIZE;
    }

    // Drive a master transfer against the slave mailbox until it ends
    void run(std::shared_ptr<mailbox::request::FoEMessage> const& foe)
    {
        for (int guard = 0; guard < 1000; ++guard)
        {
            auto msg = master.send();
            std::vector<uint8_t> reply = slave.processRequest({msg->data(), msg->data() + msg->size()});
            if (foe->status() != MessageStatus::RUNNING)
            {
                ASSERT_TRUE(reply.empty());
                return;
            }
            ASSERT_FALSE(reply.empty());
            ASSERT_EQ(MBX_SIZE, reply.size());
            master.receive(reply.data());
            if (foe->status() != MessageStatus::RUNNING)
            {
                return;
            }
        }
        FAIL() << "transfer did not end";
    }

protected:
    MemoryStorage storage;
    Mailbox slave{MBX_SIZE, 1};
    mailbox::request::Mailbox master;
};


class FoE_Roundtrip : public FoE_Response, public ::testing::WithParamInterface<std::size_t>
{
};

TEST_P(FoE_Roundtrip, read)
{
    auto file = pattern(GetParam());
    storage.files["fw.bin"] = file;

    auto foe = master.createFoERead("fw.bin", 0);
    run(foe);

    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
    ASSERT_EQ(file, foe->file());
    ASSERT_EQ(0, storage.open);
}

TEST_P(FoE_Roundtrip, write)
{
    auto file = pattern(GetParam());

    auto foe = master.createFoEWrite("fw.bin", 0, file);
    run(foe);

    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
    ASSERT_EQ(file, storage.files["fw.bin"]);
    ASSERT_EQ(file.size(), foe->bytesTransferred());
    ASSERT_EQ(1, storage.commits);
    ASSERT_EQ(0, storage.discarded);
    ASSERT_EQ(0, storage.open);
}

INSTANTIATE_TEST_SUITE_P(Sizes, FoE_Roundtrip,
    ::testing::Values(0, 1, CAPACITY - 1, CAPACITY, 3 * CAPACITY, 3 * CAPACITY + 7));


TEST_F(FoE_Response, replies_carry_the_slave_counter)
{
    storage.files["fw.bin"] = pattern(3 * CAPACITY);

    // 1 to 7, never the reserved 0, and a new value for each reply
    uint8_t previous = 0;
    auto check = [&](std::vector<uint8_t> const& reply)
    {
        uint8_t count = pointData<mailbox::Header>(reply.data())->count;
        EXPECT_NE(0, count);
        EXPECT_NE(previous, count);
        previous = count;
    };

    check(slave.processRequest(createRequest(FoE::opcode::READ, "fw.bin")));
    for (uint32_t packet = 1; packet <= 10; ++packet)
    {
        auto reply = slave.processRequest(createPDU(FoE::opcode::ACK, packet));
        if (reply.empty())
        {
            // Read done: next exchanges are new requests
            reply = slave.processRequest(createRequest(FoE::opcode::READ, "missing"));
        }
        check(reply);
    }
}

TEST_F(FoE_Response, read_not_found)
{
    auto foe = master.createFoERead("missing", 0);
    run(foe);
    ASSERT_EQ(FoE::result::NOT_FOUND, foe->status());
}

TEST_F(FoE_Response, wrong_password)
{
    storage.expected_password = 0x1234;
    storage.files["fw.bin"] = pattern(3);

    auto foe = master.createFoERead("fw.bin", 0x4321);
    run(foe);
    ASSERT_EQ(FoE::result::NO_RIGHTS, foe->status());

    foe = master.createFoERead("fw.bin", 0x1234);
    run(foe);
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
}

TEST_F(FoE_Response, write_commit_failure)
{
    storage.commit_error = FoE::result::DISK_FULL;

    auto foe = master.createFoEWrite("fw.bin", 0, pattern(CAPACITY + 3));
    run(foe);

    ASSERT_EQ(FoE::result::DISK_FULL, foe->status());
    ASSERT_EQ(0, storage.files.count("fw.bin"));
}

TEST_F(FoE_Response, new_request_supersedes_transfer)
{
    storage.files["fw.bin"] = pattern(3 * CAPACITY);

    // Start a read and give up after the first packet (e.g. the master timed out)
    auto reply = slave.processRequest(createRequest(FoE::opcode::READ, "fw.bin"));
    ASSERT_EQ(FoE::opcode::DATA, decode(reply).opcode);

    // With a single message slot, the new request must still be served
    auto foe = master.createFoEWrite("new.bin", 0, pattern(5));
    run(foe);
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
    ASSERT_EQ(0, storage.open);
    ASSERT_EQ(pattern(5), storage.files["new.bin"]);
}

TEST_F(FoE_Response, data_without_transfer)
{
    auto reply = decode(slave.processRequest(createPDU(FoE::opcode::DATA, 1, pattern(2))));
    ASSERT_EQ(mailbox::Type::FoE, reply.type);
    ASSERT_EQ(FoE::opcode::ERROR, reply.opcode);
    ASSERT_EQ(FoE::result::PACKET_NUMBER_WRONG, reply.value);

    reply = decode(slave.processRequest(createPDU(FoE::opcode::ACK, 1)));
    ASSERT_EQ(FoE::opcode::ERROR, reply.opcode);

    // An unsolicited error has nothing to abort
    ASSERT_TRUE(slave.processRequest(createPDU(FoE::opcode::ERROR, FoE::result::NOT_DEFINED)).empty());
}

TEST_F(FoE_Response, write_wrong_packet_number)
{
    auto reply = decode(slave.processRequest(createRequest(FoE::opcode::WRITE, "fw.bin")));
    ASSERT_EQ(FoE::opcode::ACK, reply.opcode);
    ASSERT_EQ(0, reply.value);

    reply = decode(slave.processRequest(createPDU(FoE::opcode::DATA, 2, pattern(CAPACITY))));
    ASSERT_EQ(FoE::opcode::ERROR, reply.opcode);
    ASSERT_EQ(FoE::result::PACKET_NUMBER_WRONG, reply.value);
    ASSERT_EQ(1, storage.discarded);
    ASSERT_EQ(0, storage.open);
}

TEST_F(FoE_Response, read_unexpected_opcode)
{
    storage.files["fw.bin"] = pattern(3 * CAPACITY);
    slave.processRequest(createRequest(FoE::opcode::READ, "fw.bin"));

    auto reply = decode(slave.processRequest(createPDU(FoE::opcode::DATA, 1, pattern(2))));
    ASSERT_EQ(FoE::opcode::ERROR, reply.opcode);
    ASSERT_EQ(FoE::result::ILLEGAL, reply.value);
    ASSERT_EQ(0, storage.open);
}

TEST_F(FoE_Response, master_abort)
{
    slave.processRequest(createRequest(FoE::opcode::WRITE, "fw.bin"));
    slave.processRequest(createPDU(FoE::opcode::DATA, 1, pattern(CAPACITY)));

    ASSERT_TRUE(slave.processRequest(createPDU(FoE::opcode::ERROR, FoE::result::NOT_DEFINED)).empty());
    ASSERT_EQ(1, storage.discarded);
    ASSERT_EQ(0, storage.files.count("fw.bin"));
}

TEST_F(FoE_Response, stopped_mailbox_closes_the_transfer)
{
    slave.processRequest(createRequest(FoE::opcode::WRITE, "fw.bin"));
    slave.processRequest(createPDU(FoE::opcode::DATA, 1, pattern(CAPACITY)));
    ASSERT_EQ(1, storage.open);

    // e.g. the master requests INIT in the middle of a firmware write
    slave.activate(false);
    ASSERT_EQ(0, storage.open);
    ASSERT_EQ(1, storage.discarded);
    ASSERT_EQ(0, storage.files.count("fw.bin"));

    // The next transfer starts from scratch
    auto foe = master.createFoEWrite("fw.bin", 0, pattern(5));
    run(foe);
    ASSERT_EQ(MessageStatus::SUCCESS, foe->status());
    ASSERT_EQ(1, storage.discarded);
}

TEST_F(FoE_Response, too_short)
{
    auto raw = createPDU(FoE::opcode::READ, 0);
    pointData<mailbox::Header>(raw.data())->len = 2;
    auto reply = slave.processRequest(std::move(raw));

    auto const* header = pointData<mailbox::Header>(reply.data());
    auto const* error  = pointData<mailbox::Error::ServiceData>(header);
    ASSERT_EQ(mailbox::Type::ERR, header->type);
    ASSERT_EQ(mailbox::Error::INVALID_SIZE, error->detail);
}

TEST_F(FoE_Response, survives_truncated_requests)
{
    storage.files["fw.bin"] = pattern(3 * CAPACITY);
    slave.processRequest(createRequest(FoE::opcode::READ, "fw.bin"));

    auto ack = createPDU(FoE::opcode::ACK, 1);
    for (std::size_t size = 0; size <= ack.size(); ++size)
    {
        std::vector<uint8_t> truncated(ack.begin(), ack.begin() + static_cast<std::ptrdiff_t>(size));
        std::vector<uint8_t> answer = slave.processRequest(std::move(truncated));
        if (not answer.empty())
        {
            EXPECT_GE(answer.size(), sizeof(mailbox::Header)) << "size " << size;
        }
    }
}

TEST(FoE_Response_Disabled, unsupported_protocol)
{
    Mailbox slave{MBX_SIZE, 1};
    auto reply = slave.processRequest(createRequest(FoE::opcode::READ, "fw.bin"));

    auto const* header = pointData<mailbox::Header>(reply.data());
    auto const* error  = pointData<mailbox::Error::ServiceData>(header);
    ASSERT_EQ(mailbox::Type::ERR, header->type);
    ASSERT_EQ(mailbox::Error::UNSUPPORTED_PROTOCOL, error->detail);
}
