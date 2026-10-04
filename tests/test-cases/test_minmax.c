#include <common/minmax.h>
#include <common/types.h>
#include <common/bit.h>

#include <test_harness.h>

TEST_CASE(within_bounds_excludes_the_end)
{
    ASSERT_FALSE(within_bounds(9u, 10u, 20u));
    ASSERT_TRUE(within_bounds(10u, 10u, 20u));
    ASSERT_TRUE(within_bounds(19u, 10u, 20u));
    ASSERT_FALSE(within_bounds(20u, 10u, 20u));
    ASSERT_FALSE(within_bounds(10u, 10u, 10u));
}

TEST_CASE(within_bounds_inclusive_includes_the_last_value)
{
    ASSERT_FALSE(within_bounds_inclusive(9u, 10u, 20u));
    ASSERT_TRUE(within_bounds_inclusive(10u, 10u, 20u));
    ASSERT_TRUE(within_bounds_inclusive(20u, 10u, 20u));
    ASSERT_FALSE(within_bounds_inclusive(21u, 10u, 20u));
    ASSERT_TRUE(within_bounds_inclusive(10u, 10u, 10u));
}

TEST_CASE(within_bounds_reaches_the_top_of_the_type)
{
    u64 max;

    max = UNSIGNED_MAX(u64);

    ASSERT_TRUE(within_bounds(max - 1, max - 1, max));
    ASSERT_FALSE(within_bounds(max, max - 1, max));
    ASSERT_TRUE(within_bounds_inclusive(max, max - 1, max));
    ASSERT_FALSE(within_bounds_inclusive((u64)0, max - 1, max));
}

TEST_CASE(within_bounds_of_narrow_types)
{
    u8 value;

    value = 200;

    ASSERT_TRUE(within_bounds(value, (u8)100, (u8)255));
    ASSERT_FALSE(within_bounds((u8)50, (u8)100, (u8)255));
    ASSERT_FALSE(within_bounds((u8)255, (u8)100, (u8)255));
    ASSERT_TRUE(within_bounds_inclusive((u8)255, (u8)100, (u8)255));
}

TEST_CASE(within_bounds_evaluates_each_argument_once)
{
    u32 value, begin, end;

    value = 5;
    begin = 0;
    end = 10;

    ASSERT_TRUE(within_bounds(value++, begin++, end++));
    ASSERT_EQ(value, 6);
    ASSERT_EQ(begin, 1);
    ASSERT_EQ(end, 11);
}

TEST_CASE(clamp_limits_a_value_to_the_range)
{
    ASSERT_EQ(CLAMP(5u, 10u, 20u), 10u);
    ASSERT_EQ(CLAMP(15u, 10u, 20u), 15u);
    ASSERT_EQ(CLAMP(25u, 10u, 20u), 20u);
}

TEST_CASE(clamp_evaluates_each_argument_once)
{
    u32 value, low, high;

    value = 5;
    low = 0;
    high = 10;

    ASSERT_EQ(CLAMP(value++, low++, high++), 5);
    ASSERT_EQ(value, 6);
    ASSERT_EQ(low, 1);
    ASSERT_EQ(high, 11);
}

TEST_CASE(non_negative_constants_of_any_width)
{
    STATIC_ASSERT(IS_UNSIGNED_OR_NON_NEGATIVE_CONSTANT((i128)1 << 100), "");
    STATIC_ASSERT(IS_UNSIGNED_OR_NON_NEGATIVE_CONSTANT((i128)1 << 63), "");
    STATIC_ASSERT(!IS_UNSIGNED_OR_NON_NEGATIVE_CONSTANT(-((i128)1 << 64)), "");
    STATIC_ASSERT(!IS_UNSIGNED_OR_NON_NEGATIVE_CONSTANT((i8)-1), "");
    STATIC_ASSERT(IS_UNSIGNED_OR_NON_NEGATIVE_CONSTANT((u128)1 << 127), "");
}

TEST_CASE(clamp_accepts_non_negative_constants_for_an_unsigned_value)
{
    u32 value;

    value = 30;

    ASSERT_EQ(CLAMP(value, 0, 20), 20u);
    ASSERT_EQ(CLAMP((u8)5, 10, 20), 10);
}

TEST_CASE(clamp_of_signed_values)
{
    i32 value;

    value = -30;

    ASSERT_EQ(CLAMP(value, -20, 20), -20);
    ASSERT_EQ(CLAMP(-value, -20, 20), 20);
}

TEST_CASE(clamp_of_constants_is_a_constant)
{
    STATIC_ASSERT(CLAMP(30u, 10u, 20u) == 20u, "clamped to the high limit");
    STATIC_ASSERT(CLAMP(-30, -20, 20) == -20, "clamped to the low limit");
}
