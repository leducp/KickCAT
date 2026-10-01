#include <gtest/gtest.h>

#include "kickcat/ENI/Config.h"

using namespace kickcat;

TEST(ENIConfig, transition_string_round_trip)
{
    for (uint16_t bit = 0; bit < 15; ++bit)
    {
        ENI::transition::Type t = static_cast<ENI::transition::Type>(1 << bit);
        ASSERT_EQ(ENI::transition::fromString(ENI::transition::toString(t)), t);
    }
    ASSERT_STREQ(ENI::transition::toString(ENI::transition::SO), "SO");
    ASSERT_THROW(ENI::transition::fromString("XX"), std::invalid_argument);
    ASSERT_THROW(ENI::transition::fromString(""),   std::invalid_argument);
}

TEST(ENIConfig, transition_between_states)
{
    ASSERT_EQ(ENI::transition::between(State::INIT,        State::PRE_OP),      ENI::transition::IP);
    ASSERT_EQ(ENI::transition::between(State::PRE_OP,      State::SAFE_OP),     ENI::transition::PS);
    ASSERT_EQ(ENI::transition::between(State::SAFE_OP,     State::OPERATIONAL), ENI::transition::SO);
    ASSERT_EQ(ENI::transition::between(State::OPERATIONAL, State::INIT),        ENI::transition::OI);
    ASSERT_EQ(ENI::transition::between(State::INIT,        State::BOOT),        ENI::transition::IB);
    ASSERT_EQ(ENI::transition::between(State::BOOT,        State::PRE_OP),      ENI::transition::NONE);
    ASSERT_EQ(ENI::transition::between(State::INIT,        State::OPERATIONAL), ENI::transition::NONE);
}

TEST(ENIConfig, state_from_al_state)
{
    ASSERT_EQ(ENI::state::from(State::INIT),        ENI::state::INIT);
    ASSERT_EQ(ENI::state::from(State::PRE_OP),      ENI::state::PREOP);
    ASSERT_EQ(ENI::state::from(State::SAFE_OP),     ENI::state::SAFEOP);
    ASSERT_EQ(ENI::state::from(State::OPERATIONAL), ENI::state::OP);
    ASSERT_EQ(ENI::state::from(static_cast<State>(State::SAFE_OP | State::ERROR_ACK)), ENI::state::SAFEOP);
    ASSERT_EQ(ENI::state::from(State::BOOT),        ENI::state::NONE);
}

TEST(ENIConfig, command_address)
{
    ENI::InitCmd cmd;
    cmd.adp = 0xFFFF;
    cmd.ado = 0x0120;
    ASSERT_EQ(cmd.address(), createAddress(0xFFFF, 0x0120));

    cmd.logical_address = 0x10000;
    ASSERT_EQ(cmd.address(), 0x10000u);

    ENI::CyclicCmd cyclic;
    cyclic.adp = 0;
    cyclic.ado = 0x0130;
    ASSERT_EQ(cyclic.address(), createAddress(0, 0x0130));
    cyclic.logical_address = 0x12000;
    ASSERT_EQ(cyclic.address(), 0x12000u);
}

namespace
{
    ENI::Validate makeValidate(std::vector<uint8_t> data, std::vector<uint8_t> mask, ENI::compare::Type type, bool is_signed = false)
    {
        ENI::Validate v;
        v.data = std::move(data);
        v.mask = std::move(mask);
        v.type = type;
        v.is_signed = is_signed;
        return v;
    }

    bool matches(ENI::Validate const& v, std::vector<uint8_t> const& reply)
    {
        return v.matches(reply.data(), reply.size());
    }
}

TEST(ENIConfig, validate_compares_every_byte)
{
    std::vector<uint8_t> expected(10, 0);
    std::vector<uint8_t> reply(10, 0);
    ENI::Validate eq = makeValidate(expected, {}, ENI::compare::EQ);
    ASSERT_TRUE(matches(eq, reply));

    reply[9] = 1;
    ASSERT_FALSE(matches(eq, reply));
    ASSERT_TRUE(matches(makeValidate(expected, {}, ENI::compare::G), reply));
}

TEST(ENIConfig, validate_mask)
{
    // AL status 0x0014: SAFEOP with the error bit set
    std::vector<uint8_t> al_status = {0x14, 0x00};
    ASSERT_FALSE(matches(makeValidate({0x04, 0x00}, {0x1f, 0x00}, ENI::compare::EQ), al_status));
    ASSERT_TRUE (matches(makeValidate({0x04, 0x00}, {0x0f, 0x00}, ENI::compare::EQ), al_status));
    ASSERT_TRUE (matches(makeValidate({0x04, 0x00}, {},           ENI::compare::NOT_EQ), al_status));
}

TEST(ENIConfig, validate_order_is_little_endian)
{
    std::vector<uint8_t> reply = {0x00, 0x02};   // 0x0200
    ENI::Validate v = makeValidate({0xff, 0x01}, {}, ENI::compare::G);   // 0x01ff
    ASSERT_TRUE(matches(v, reply));

    v.type = ENI::compare::EQ_OR_L;
    ASSERT_FALSE(matches(v, reply));
    v.type = ENI::compare::L;
    ASSERT_FALSE(matches(v, reply));
    v.type = ENI::compare::EQ_OR_G;
    ASSERT_TRUE(matches(v, reply));
    v.type = ENI::compare::NONE;
    ASSERT_TRUE(matches(v, reply));
}

TEST(ENIConfig, validate_signed)
{
    std::vector<uint8_t> minus_one = {0xff};
    ASSERT_TRUE (matches(makeValidate({0x00}, {}, ENI::compare::G, false), minus_one));
    ASSERT_FALSE(matches(makeValidate({0x00}, {}, ENI::compare::G, true),  minus_one));
    ASSERT_TRUE (matches(makeValidate({0x00}, {}, ENI::compare::L, true),  minus_one));

    // -32768 < 1, only the most significant byte carries the sign
    std::vector<uint8_t> min16 = {0x00, 0x80};
    ASSERT_TRUE (matches(makeValidate({0x01, 0x00}, {}, ENI::compare::L, true),  min16));
    ASSERT_FALSE(matches(makeValidate({0x01, 0x00}, {}, ENI::compare::L, false), min16));
    std::vector<uint8_t> minus_two = {0xfe, 0xff};
    ASSERT_TRUE (matches(makeValidate({0xff, 0xff}, {}, ENI::compare::L, true),  minus_two));
}

TEST(ENIConfig, validate_uses_the_validate_width)
{
    std::vector<uint8_t> reply = {0x02, 0x00, 0xAA, 0xBB};
    ASSERT_TRUE(matches(makeValidate({0x02, 0x00}, {0x1f, 0x00}, ENI::compare::EQ), reply));

    std::vector<uint8_t> short_reply = {0x02};
    ASSERT_FALSE(matches(makeValidate({0x02, 0x00}, {0x1f, 0x00}, ENI::compare::EQ), short_reply));
    ASSERT_FALSE(matches(makeValidate({0x02, 0x00}, {0x1f, 0x00}, ENI::compare::NONE), short_reply));
}

TEST(ENIConfig, validate_mask_applies_to_the_reply_only)
{
    // ETG.2100: reply AND mask, then compare with Data as written
    std::vector<uint8_t> reply = {0x0F};
    ASSERT_TRUE (matches(makeValidate({0x15}, {0x0f}, ENI::compare::L),  reply));
    ASSERT_FALSE(matches(makeValidate({0x15}, {0x0f}, ENI::compare::EQ), reply));

    // A mask shorter than the data leaves the remaining bytes compared whole
    std::vector<uint8_t> al_status = {0x14, 0x02};
    ASSERT_TRUE (matches(makeValidate({0x04, 0x02}, {0x0f}, ENI::compare::EQ), al_status));
    ASSERT_FALSE(matches(makeValidate({0x04, 0x00}, {0x0f}, ENI::compare::EQ), al_status));
}
