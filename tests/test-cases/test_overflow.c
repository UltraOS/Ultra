#include <common/overflow.h>
#include <common/types.h>
#include <common/bit.h>

#include <test_harness.h>

TEST_CASE(check_add_overflow_reports_a_wrap)
{
    u64 result;

    ASSERT_FALSE(check_add_overflow(UNSIGNED_MAX(u64) - 1, 1u, &result));
    ASSERT_EQ(result, UNSIGNED_MAX(u64));

    ASSERT_TRUE(check_add_overflow(UNSIGNED_MAX(u64), 2u, &result));
    ASSERT_EQ(result, 1u);
}

TEST_CASE(check_sub_overflow_reports_a_wrap)
{
    u32 result;

    ASSERT_FALSE(check_sub_overflow(5u, 5u, &result));
    ASSERT_EQ(result, 0u);

    ASSERT_TRUE(check_sub_overflow(5u, 6u, &result));
    ASSERT_EQ(result, UNSIGNED_MAX(u32));
}

TEST_CASE(check_mul_overflow_reports_a_wrap)
{
    u32 result;

    ASSERT_FALSE(check_mul_overflow(0x10000u, 0xFFFFu, &result));
    ASSERT_EQ(result, 0xFFFF0000u);

    ASSERT_TRUE(check_mul_overflow(0x10000u, 0x10000u, &result));
    ASSERT_EQ(result, 0u);
}

TEST_CASE(check_overflow_judges_by_the_result_type)
{
    u8 narrow;
    i32 value;

    ASSERT_TRUE(check_add_overflow(200ull, 100ull, &narrow));
    ASSERT_EQ(narrow, 44);

    ASSERT_FALSE(check_sub_overflow(0u, 1u, &value));
    ASSERT_EQ(value, -1);
}
