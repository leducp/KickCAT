#include <gtest/gtest.h>

#include <cstring>

#include "kickcat/EoE/FrameReassembler.h"

using namespace kickcat;

static std::vector<uint8_t> makeFrame(size_t n)
{
    std::vector<uint8_t> f(n);
    for (size_t i = 0; i < n; ++i)
    {
        f[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    }
    return f;
}

static std::vector<uint8_t> roundtrip(std::vector<uint8_t> const& frame, uint16_t mailbox_size)
{
    auto fragments = EoE::fragmentFrame(frame.data(), frame.size(), mailbox_size, 5, 1);
    EoE::FrameReassembler reassembler;
    std::vector<uint8_t> out;
    for (auto const& fragment : fragments)
    {
        auto result = reassembler.push(fragment.data(), fragment.size());
        if (result.has_value())
        {
            out = *result;
        }
    }
    return out;
}

TEST(EoE_FrameReassembler, single_fragment)
{
    auto frame = makeFrame(20);
    auto fragments = EoE::fragmentFrame(frame.data(), frame.size(), 128, 0, 0);
    ASSERT_EQ(1u, fragments.size());

    auto out = roundtrip(frame, 128);
    ASSERT_EQ(frame, out);
}

TEST(EoE_FrameReassembler, block_aligned_frame)
{
    auto frame = makeFrame(96);   // exactly three 32-octet blocks
    ASSERT_EQ(frame, roundtrip(frame, 128));
}

TEST(EoE_FrameReassembler, multi_fragment_max_size)
{
    auto frame = makeFrame(1500);
    auto fragments = EoE::fragmentFrame(frame.data(), frame.size(), 128, 7, 0);
    ASSERT_GT(fragments.size(), 1u);
    ASSERT_EQ(frame, roundtrip(frame, 128));
}

TEST(EoE_FrameReassembler, fragments_are_block_aligned_except_last)
{
    auto frame = makeFrame(700);
    auto fragments = EoE::fragmentFrame(frame.data(), frame.size(), 128, 1, 0);
    for (size_t i = 0; i + 1 < fragments.size(); ++i)
    {
        auto const* header = pointData<mailbox::Header>(fragments[i].data());
        size_t data_len = header->len - sizeof(EoE::Header);
        ASSERT_EQ(0u, data_len % 32) << "fragment " << i << " not block-aligned";
    }
}

TEST(EoE_FrameReassembler, gap_rejects_and_resets)
{
    auto frame = makeFrame(600);
    auto fragments = EoE::fragmentFrame(frame.data(), frame.size(), 128, 1, 0);
    ASSERT_GE(fragments.size(), 3u);

    EoE::FrameReassembler reassembler;
    ASSERT_FALSE(reassembler.push(fragments[0].data(), fragments[0].size()).has_value());
    // skipping fragment 1 leaves a hole; fragment 2's offset will not match
    ASSERT_FALSE(reassembler.push(fragments[2].data(), fragments[2].size()).has_value());
}

TEST(EoE_FrameReassembler, fragment_without_initiate_is_dropped)
{
    auto frame = makeFrame(600);
    auto fragments = EoE::fragmentFrame(frame.data(), frame.size(), 128, 1, 0);
    ASSERT_GE(fragments.size(), 2u);

    EoE::FrameReassembler reassembler;
    ASSERT_FALSE(reassembler.push(fragments[1].data(), fragments[1].size()).has_value());
}

TEST(EoE_FrameReassembler, oversized_declared_length_is_rejected)
{
    // header->len far exceeds the actual buffer: the reassembler must not over-read.
    std::vector<uint8_t> raw(64, 0);
    auto* header = pointData<mailbox::Header>(raw.data());
    auto* eoe    = pointData<EoE::Header>(header);
    header->type = mailbox::Type::EoE;
    header->len  = 0xFFFF;
    eoe->type    = EoE::frame_type::FRAGMENT;

    EoE::FrameReassembler reassembler;
    ASSERT_FALSE(reassembler.push(raw.data(), raw.size()).has_value());
}

TEST(EoE_FrameReassembler, non_eoe_message_is_ignored)
{
    std::vector<uint8_t> raw(64, 0);
    auto* header = pointData<mailbox::Header>(raw.data());
    header->type = mailbox::Type::CoE;
    header->len  = sizeof(EoE::Header);

    EoE::FrameReassembler reassembler;
    ASSERT_FALSE(reassembler.push(raw.data(), raw.size()).has_value());
}

TEST(EoE_FrameReassembler, reused_after_completion)
{
    auto first  = makeFrame(300);
    auto second = makeFrame(64);
    EoE::FrameReassembler reassembler;

    auto run = [&](std::vector<uint8_t> const& frame)
    {
        auto fragments = EoE::fragmentFrame(frame.data(), frame.size(), 128, 2, 0);
        std::vector<uint8_t> out;
        for (auto const& fragment : fragments)
        {
            auto result = reassembler.push(fragment.data(), fragment.size());
            if (result.has_value())
            {
                out = *result;
            }
        }
        return out;
    };

    ASSERT_EQ(first, run(first));
    ASSERT_EQ(second, run(second));
}

TEST(EoE_FrameReassembler, mailbox_too_small_throws)
{
    auto frame = makeFrame(20);
    ASSERT_THROW(EoE::fragmentFrame(frame.data(), frame.size(), 8, 0, 0), std::exception);
}

// Append a 4-octet timestamp to the last fragment and flag time_appended, as a compliant
// peer requesting/reporting a timestamp would.
static void appendTimestamp(std::vector<std::vector<uint8_t>>& fragments, uint32_t ts)
{
    auto& last = fragments.back();
    auto* header = pointData<mailbox::Header>(last.data());
    auto* eoe    = pointData<EoE::Header>(header);
    eoe->time_appended = 1;
    header->len = static_cast<uint16_t>(header->len + sizeof(uint32_t));
    uint8_t const* p = reinterpret_cast<uint8_t const*>(&ts);
    last.insert(last.end(), p, p + sizeof(uint32_t));
}

TEST(EoE_FrameReassembler, timestamp_trailer_is_stripped_multi_fragment)
{
    auto frame = makeFrame(700);
    auto fragments = EoE::fragmentFrame(frame.data(), frame.size(), 128, 1, 0);
    ASSERT_GT(fragments.size(), 1u);
    appendTimestamp(fragments, 0xDEADBEEF);

    EoE::FrameReassembler reassembler;
    std::vector<uint8_t> out;
    for (auto const& fragment : fragments)
    {
        auto result = reassembler.push(fragment.data(), fragment.size());
        if (result.has_value())
        {
            out = *result;
        }
    }
    ASSERT_EQ(frame, out);   // the 4 timestamp octets must not pollute the frame
}

TEST(EoE_FrameReassembler, timestamp_trailer_is_stripped_single_fragment)
{
    auto frame = makeFrame(40);
    auto fragments = EoE::fragmentFrame(frame.data(), frame.size(), 128, 0, 0);
    ASSERT_EQ(1u, fragments.size());
    appendTimestamp(fragments, 0x12345678);

    EoE::FrameReassembler reassembler;
    auto result = reassembler.push(fragments[0].data(), fragments[0].size());
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(frame, *result);
}

TEST(EoE_FrameReassembler, interleaved_frames_same_port)
{
    auto frame_a = makeFrame(600);
    auto frame_b = makeFrame(600);
    auto a = EoE::fragmentFrame(frame_a.data(), frame_a.size(), 128, 3, 0);
    auto b = EoE::fragmentFrame(frame_b.data(), frame_b.size(), 128, 4, 0);
    ASSERT_EQ(a.size(), b.size());
    ASSERT_GT(a.size(), 1u);

    EoE::FrameReassembler reassembler;
    std::vector<uint8_t> got_a;
    std::vector<uint8_t> got_b;
    for (size_t i = 0; i < a.size(); ++i)
    {
        auto ra = reassembler.push(a[i].data(), a[i].size());
        if (ra.has_value()) { got_a = *ra; }
        auto rb = reassembler.push(b[i].data(), b[i].size());
        if (rb.has_value()) { got_b = *rb; }
    }
    ASSERT_EQ(frame_a, got_a);
    ASSERT_EQ(frame_b, got_b);
}

TEST(EoE_FrameReassembler, interleaved_frames_distinct_ports)
{
    auto frame_a = makeFrame(300);
    auto frame_b = makeFrame(300);
    auto a = EoE::fragmentFrame(frame_a.data(), frame_a.size(), 128, 0, 1);  // same frame number...
    auto b = EoE::fragmentFrame(frame_b.data(), frame_b.size(), 128, 0, 2);  // ...different port
    ASSERT_EQ(a.size(), b.size());
    ASSERT_GT(a.size(), 1u);

    EoE::FrameReassembler reassembler;
    std::vector<uint8_t> got_a;
    std::vector<uint8_t> got_b;
    for (size_t i = 0; i < a.size(); ++i)
    {
        auto ra = reassembler.push(a[i].data(), a[i].size());
        if (ra.has_value()) { got_a = *ra; }
        auto rb = reassembler.push(b[i].data(), b[i].size());
        if (rb.has_value()) { got_b = *rb; }
    }
    ASSERT_EQ(frame_a, got_a);
    ASSERT_EQ(frame_b, got_b);
}
