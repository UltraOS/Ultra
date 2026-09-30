#include <common/types.h>

#include <test_harness.h>

#define TEST_SHIFT 40

#if LITERAL_OF_TYPE(u32, 5) != 5
#error LITERAL_OF_TYPE() is not usable in #if
#endif

TEST_CASE(literal_of_type_gives_the_type)
{
    ASSERT(ARE_SAME_TYPE(LITERAL_OF_TYPE(i32, 1), i32));
    ASSERT(ARE_SAME_TYPE(LITERAL_OF_TYPE(i64, 1), i64));
    ASSERT(ARE_SAME_TYPE(LITERAL_OF_TYPE(u32, 1), u32));
    ASSERT(ARE_SAME_TYPE(LITERAL_OF_TYPE(u64, 1), u64));
    ASSERT(ARE_SAME_TYPE(LITERAL_OF_TYPE(reg_t, 1), reg_t));
    ASSERT(ARE_SAME_TYPE(LITERAL_OF_TYPE(phys_addr_t, 1), phys_addr_t));
    ASSERT(ARE_SAME_TYPE(LITERAL_OF_TYPE(reg_t, 1) << 3, reg_t));
}

TEST_CASE(literal_of_type_values)
{
    ASSERT_EQ((LITERAL_OF_TYPE(u64, 1) << TEST_SHIFT), 0x10000000000ull);
    ASSERT_EQ((LITERAL_OF_TYPE(u32, 0xFFFFFFFF) + 1), 0);
    ASSERT_EQ((~LITERAL_OF_TYPE(u64, 0xFFF)), 0xFFFFFFFFFFFFF000ull);
    ASSERT_EQ(LITERAL_OF_TYPE(reg_t, TEST_SHIFT), 40);
    ASSERT_EQ(LITERAL_OF_TYPE(i32, -1), -1);
}

TEST_CASE(const_of_type_casts)
{
    ASSERT(ARE_SAME_TYPE(CONST_OF_TYPE(u8, 1), u8));
    ASSERT(ARE_SAME_TYPE(CONST_OF_TYPE(reg_t, TEST_SHIFT - 8), reg_t));

    ASSERT_EQ(CONST_OF_TYPE(u8, 0x1FF), 0xFF);
    ASSERT_EQ(CONST_OF_TYPE(u16, 1 << 4), 16);
    ASSERT_EQ((CONST_OF_TYPE(u64, 1) << TEST_SHIFT), 0x10000000000ull);
}
