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
