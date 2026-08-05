#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "kickcat/utils/math.h"

using namespace kickcat;

// Bounds wider than every destination type used here, so these cases reach the type reduction the
// way a caller with no meaningful limit of its own would.
constexpr double WIDE_LO = -1e10;
constexpr double WIDE_HI = 1e10;

// The out-of-range cases below are undefined behaviour with a plain static_cast.

TEST(NumericConversion, in_range_truncates_toward_zero)
{
    EXPECT_EQ(1,  saturate<int32_t>(1.9, WIDE_LO, WIDE_HI));
    EXPECT_EQ(-1, saturate<int32_t>(-1.9, WIDE_LO, WIDE_HI));
    EXPECT_EQ(0,  saturate<int32_t>(0.0, WIDE_LO, WIDE_HI));
}


TEST(NumericConversion, caller_bounds_clamp_inside_them)
{
    EXPECT_EQ(1000,  saturate<int32_t>(5000.0, -1000.0, 1000.0));
    EXPECT_EQ(-1000, saturate<int32_t>(-5000.0, -1000.0, 1000.0));
    EXPECT_EQ(250,   saturate<int32_t>(250.0, -1000.0, 1000.0));
}


TEST(NumericConversion, caller_bounds_wider_than_the_type_are_reduced_to_it)
{
    // Losing the type bound would put the conversion back in undefined territory.
    EXPECT_EQ(INT32_MAX, saturate<int32_t>(3e9, WIDE_LO, WIDE_HI));
    EXPECT_EQ(INT32_MIN, saturate<int32_t>(-3e9, WIDE_LO, WIDE_HI));
    EXPECT_EQ(INT16_MAX, saturate<int16_t>(1e9, 1e8, 2e9));    // whole range above the type
    EXPECT_EQ(INT16_MIN, saturate<int16_t>(-1e9, -2e9, -1e8)); // whole range below the type
    EXPECT_EQ(255,       saturate<uint8_t>(300.0, WIDE_LO, WIDE_HI));
    EXPECT_EQ(0,         saturate<uint8_t>(-5.0, WIDE_LO, WIDE_HI));
}


TEST(NumericConversion, exact_bounds_are_preserved)
{
    // The 32 bit bounds are exactly representable in a double, so clamping must not round them
    // past the destination range.
    EXPECT_EQ(INT32_MAX,  saturate<int32_t>(2147483647.0, WIDE_LO, WIDE_HI));
    EXPECT_EQ(INT32_MIN,  saturate<int32_t>(-2147483648.0, WIDE_LO, WIDE_HI));
    EXPECT_EQ(UINT32_MAX, saturate<uint32_t>(4294967295.0, WIDE_LO, WIDE_HI));
}


TEST(NumericConversion, inverted_caller_bounds_describe_the_same_interval)
{
    EXPECT_EQ(100,  saturate<int32_t>(5000.0, 100.0, -100.0));
    EXPECT_EQ(-100, saturate<int32_t>(-5000.0, 100.0, -100.0));
    EXPECT_EQ(0,    saturate<int32_t>(0.0, 100.0, -100.0));
}


TEST(NumericConversion, narrowing_to_float_clamps_at_the_float_range)
{
    // double -> float is undefined outside the float range too, and the negative bound must come
    // from lowest(), not min() (which is the smallest positive normal).
    constexpr double lo = std::numeric_limits<double>::lowest();
    constexpr double hi = std::numeric_limits<double>::max();

    EXPECT_EQ(std::numeric_limits<float>::max(),    saturate<float>(1e300, lo, hi));
    EXPECT_EQ(std::numeric_limits<float>::lowest(), saturate<float>(-1e300, lo, hi));
    EXPECT_FLOAT_EQ(-1.5f,  saturate<float>(-1.5, lo, hi));
    EXPECT_FLOAT_EQ(2.5f,   saturate<float>(2.5, -10.0, 10.0));
    EXPECT_FLOAT_EQ(-10.0f, saturate<float>(-1e300, -10.0, 10.0));
}


TEST(NumericConversion, usable_in_a_constant_expression)
{
    static_assert(saturate<int32_t>(3e9, WIDE_LO, WIDE_HI) == INT32_MAX);
    static_assert(saturate<int16_t>(-40000.0, WIDE_LO, WIDE_HI) == INT16_MIN);
    static_assert(saturate<int16_t>(500.0, -100.0, 100.0) == 100);
}


TEST(NumericConversion, sixty_four_bit_destinations_saturate_at_their_exact_bound)
{
    // numeric_limits<int64_t>::max() has no exact double image (it rounds up to 2^63), so the bound
    // is compared against and the exact integer returned rather than converted back.
    constexpr double lo = std::numeric_limits<double>::lowest();
    constexpr double hi = std::numeric_limits<double>::max();

    EXPECT_EQ(INT64_MAX,  saturate<int64_t>(1e300, lo, hi));
    EXPECT_EQ(INT64_MIN,  saturate<int64_t>(-1e300, lo, hi));
    EXPECT_EQ(UINT64_MAX, saturate<uint64_t>(1e300, lo, hi));
    EXPECT_EQ(0u,         saturate<uint64_t>(-1e300, lo, hi));
    EXPECT_EQ(5,          saturate<int64_t>(5.5, lo, hi));
    EXPECT_EQ(-5,         saturate<int64_t>(-5.5, lo, hi));
    EXPECT_EQ(1000,       saturate<int64_t>(1e300, -1000.0, 1000.0));
}


TEST(Math, round_to_int_rounds_to_nearest_and_saturates)
{
    EXPECT_EQ(2,  round_to_int(1.5));
    EXPECT_EQ(-2, round_to_int(-1.5));
    EXPECT_EQ(1,  round_to_int(1.4));
    EXPECT_EQ(0,  round_to_int(0.0));

    // A bare cast of these would be undefined; the destination range is the only bound available.
    EXPECT_EQ(INT64_MAX, round_to_int(1e300));
    EXPECT_EQ(INT64_MIN, round_to_int(-1e300));

    static_assert(round_to_int(1.5) == 2);
    static_assert(round_to_int(1e300) == INT64_MAX);
}


TEST(Math, clamp_and_abs_are_usable_in_a_constant_expression)
{
    static_assert(kickcat::clamp(5, 0, 3) == 3);
    static_assert(kickcat::clamp(-5, 0, 3) == 0);
    static_assert(abs_value(-7) == 7);
    static_assert(abs_value(7) == 7);
}
