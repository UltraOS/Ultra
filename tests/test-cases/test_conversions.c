#include <common/conversions.h>

#include <test_harness.h>

TEST_CASE(conversions_empty_is_not_a_number)
{
    u64 value = 1;
    i64 signed_value = 1;

    ASSERT_NE(str_to_u64(STR(""), &value), EOK);
    ASSERT_NE(str_to_u64_with_base(STR(""), &value, 10), EOK);
    ASSERT_NE(str_to_u64_with_base(STR(""), &value, 16), EOK);
    ASSERT_NE(str_to_u64(STR("+"), &value), EOK);
    ASSERT_NE(str_to_u64(STR("0x"), &value), EOK);
    ASSERT_NE(str_to_u64(STR("0b"), &value), EOK);
    ASSERT_NE(str_to_i64(STR("-"), &signed_value), EOK);
    ASSERT_NE(str_to_i64_with_base(STR("-"), &signed_value, 10), EOK);
    ASSERT_EQ(value, 1);
    ASSERT_EQ(signed_value, 1);
}

TEST_CASE(conversions_zero)
{
    u64 value = 1;

    ASSERT_EQ(str_to_u64(STR("0"), &value), EOK);
    ASSERT_EQ(value, 0);
    ASSERT_EQ(str_to_u64(STR("00"), &value), EOK);
    ASSERT_EQ(value, 0);
    ASSERT_EQ(str_to_u64(STR("0x0"), &value), EOK);
    ASSERT_EQ(value, 0);
    ASSERT_EQ(str_to_u64_with_base(STR("0"), &value, 16), EOK);
    ASSERT_EQ(value, 0);
}

TEST_CASE(conversions_bases)
{
    u64 value;

    ASSERT_EQ(str_to_u64(STR("255"), &value), EOK);
    ASSERT_EQ(value, 255);
    ASSERT_EQ(str_to_u64(STR("0xff"), &value), EOK);
    ASSERT_EQ(value, 255);
    ASSERT_EQ(str_to_u64(STR("0377"), &value), EOK);
    ASSERT_EQ(value, 255);
    ASSERT_EQ(str_to_u64(STR("0b11111111"), &value), EOK);
    ASSERT_EQ(value, 255);
    ASSERT_EQ(str_to_u64_with_base(STR("ff"), &value, 16), EOK);
    ASSERT_EQ(value, 255);
}

TEST_CASE(conversions_digits_outside_the_base)
{
    u64 value;

    ASSERT_NE(str_to_u64(STR("1a"), &value), EOK);
    ASSERT_NE(str_to_u64(STR("08"), &value), EOK);
    ASSERT_NE(str_to_u64(STR("0b12"), &value), EOK);
    ASSERT_NE(str_to_u64_with_base(STR("9"), &value, 8), EOK);
    ASSERT_NE(str_to_u64(STR("12x"), &value), EOK);
}
