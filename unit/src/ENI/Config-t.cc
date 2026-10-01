#include <gtest/gtest.h>

#include "kickcat/ENI/Config.h"

using namespace kickcat;

namespace
{
    ENI::Validate makeValidate(std::vector<uint8_t> data, std::vector<uint8_t> mask)
    {
        ENI::Validate v;
        v.data = std::move(data);
        v.mask = std::move(mask);
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
    ENI::Validate eq = makeValidate(expected, {});
    ASSERT_TRUE(matches(eq, reply));

    reply[9] = 1;
    ASSERT_FALSE(matches(eq, reply));
    reply[9] = 0;
    reply[0] = 1;
    ASSERT_FALSE(matches(eq, reply));
}

TEST(ENIConfig, validate_mask)
{
    // AL status 0x0014: SAFEOP with the error bit set
    std::vector<uint8_t> al_status = {0x14, 0x00};
    ASSERT_FALSE(matches(makeValidate({0x04, 0x00}, {0x1f, 0x00}), al_status));
    ASSERT_TRUE (matches(makeValidate({0x04, 0x00}, {0x0f, 0x00}), al_status));
}

TEST(ENIConfig, validate_uses_the_validate_width)
{
    std::vector<uint8_t> reply = {0x02, 0x00, 0xAA, 0xBB};
    ASSERT_TRUE(matches(makeValidate({0x02, 0x00}, {0x1f, 0x00}), reply));

    std::vector<uint8_t> short_reply = {0x02};
    ASSERT_FALSE(matches(makeValidate({0x02, 0x00}, {0x1f, 0x00}), short_reply));
}

TEST(ENIConfig, validate_mask_applies_to_the_reply_only)
{
    // ETG.2100: reply AND mask, then compare with Data as written
    std::vector<uint8_t> reply = {0x15};
    ASSERT_FALSE(matches(makeValidate({0x15}, {0x0f}), reply));
    ASSERT_TRUE (matches(makeValidate({0x05}, {0x0f}), reply));

    // A mask shorter than the data leaves the remaining bytes compared whole
    std::vector<uint8_t> al_status = {0x14, 0x02};
    ASSERT_TRUE (matches(makeValidate({0x04, 0x02}, {0x0f}), al_status));
    ASSERT_FALSE(matches(makeValidate({0x04, 0x00}, {0x0f}), al_status));
}
