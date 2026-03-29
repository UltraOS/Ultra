#include <common/bit.h>
#include <test_harness.h>

TEST_CASE(bit_constants_and_single_bit_macros)
{
    ASSERT_EQ(BITS_PER_BYTE, CHAR_BIT);
    ASSERT_EQ(BITS_PER_TYPE(u8), 8);
    ASSERT_EQ(BITS_PER_TYPE(u16), 16);
    ASSERT_EQ(BITS_PER_TYPE(u32), 32);
    ASSERT_EQ(BITS_PER_TYPE(u64), 64);
    ASSERT_EQ(BITS_PER_TYPE(reg_t), BITS_PER_UNIT);

    ASSERT_EQ(BIT_U8(0), (u8)0x01);
    ASSERT_EQ(BIT_U8(7), (u8)0x80);
    ASSERT_EQ(BIT_U16(9), (u16)0x0200);
    ASSERT_EQ(BIT_U32(31), 0x80000000u);
    ASSERT_EQ(BIT_U64(63), 0x8000000000000000ull);
}

TEST_CASE(bit_mask_helpers)
{
    ASSERT_EQ(BIT_U8(0), (u8)0x01);
    ASSERT_EQ(MAKE_BIT_MASK_U8(7, 0), (u8)0xFF);
    ASSERT_EQ(MAKE_BIT_MASK_U8(5, 2), (u8)0x3C);

    ASSERT_EQ(MAKE_BIT_MASK_U16(12, 8), (u16)0x1F00);
    ASSERT_EQ(MAKE_BIT_MASK_U32(31, 16), 0xFFFF0000u);
    ASSERT_EQ(MAKE_BIT_MASK_U64(63, 32), 0xFFFFFFFF00000000ull);
}

TEST_CASE(bit_field_shift_make_read_write_max_fits)
{
    u32 data;

    #define FIELD_LOW_MASK MAKE_BIT_MASK_U32(4, 0)
    #define FIELD_MID_MASK MAKE_BIT_MASK_U32(11, 8)
    #define FIELD_HI_MASK  MAKE_BIT_MASK_U32(31, 28)

    ASSERT_EQ(BIT_FIELD_SHIFT(FIELD_LOW_MASK), 0);
    ASSERT_EQ(BIT_FIELD_SHIFT(FIELD_MID_MASK), 8);
    ASSERT_EQ(BIT_FIELD_SHIFT(FIELD_HI_MASK), 28);

    ASSERT_EQ(BIT_FIELD_MAX(FIELD_LOW_MASK), 0x1Fu);
    ASSERT_EQ(BIT_FIELD_MAX(FIELD_MID_MASK), 0x0Fu);
    ASSERT_EQ(BIT_FIELD_MAX(FIELD_HI_MASK), 0x0Fu);

    ASSERT(BIT_FIELD_FITS(FIELD_LOW_MASK, 0x1F));
    ASSERT(!BIT_FIELD_FITS(FIELD_LOW_MASK, 0x20));
    ASSERT(BIT_FIELD_FITS(FIELD_MID_MASK, 0x0B));
    ASSERT(!BIT_FIELD_FITS(FIELD_MID_MASK, 0x10));

    ASSERT_EQ(BIT_FIELD_MAKE(FIELD_LOW_MASK, 0x1D), 0x0000001Du);
    ASSERT_EQ(BIT_FIELD_MAKE(FIELD_MID_MASK, 0x0A), 0x00000A00u);
    ASSERT_EQ(BIT_FIELD_MAKE(FIELD_HI_MASK, 0x0C), 0xC0000000u);

    data = 0;
    BIT_FIELD_WRITE(data, FIELD_LOW_MASK, 0x12);
    ASSERT_EQ(data, 0x12u);
    BIT_FIELD_WRITE(data, FIELD_MID_MASK, 0x0A);
    ASSERT_EQ(data, 0x00000A12u);
    BIT_FIELD_WRITE(data, FIELD_HI_MASK, 0x0D);
    ASSERT_EQ(data, 0xD0000A12u);

    ASSERT_EQ(BIT_FIELD_READ(data, FIELD_LOW_MASK), 0x12u);
    ASSERT_EQ(BIT_FIELD_READ(data, FIELD_MID_MASK), 0x0Au);
    ASSERT_EQ(BIT_FIELD_READ(data, FIELD_HI_MASK), 0x0Du);
}

TEST_CASE(bit_array_indexing_helpers)
{
    ASSERT_EQ(BIT_UNIT(0), 0u);
    ASSERT_EQ(BIT_INDEX(0), 0u);
    ASSERT_EQ(BIT_MASK_IN_UNIT(0), (reg_t)1);

    ASSERT_EQ(BIT_UNIT(BITS_PER_UNIT - 1), 0u);
    ASSERT_EQ(BIT_INDEX(BITS_PER_UNIT - 1), BITS_PER_UNIT - 1);
    ASSERT_EQ(BIT_MASK_IN_UNIT(BITS_PER_UNIT - 1), BIT(BITS_PER_UNIT - 1));

    ASSERT_EQ(BIT_UNIT(BITS_PER_UNIT), 1u);
    ASSERT_EQ(BIT_INDEX(BITS_PER_UNIT), 0u);
    ASSERT_EQ(BIT_MASK_IN_UNIT(BITS_PER_UNIT), (reg_t)1);

    ASSERT_EQ(BIT_UNIT(BITS_PER_UNIT + 5), 1u);
    ASSERT_EQ(BIT_INDEX(BITS_PER_UNIT + 5), 5u);
    ASSERT_EQ(BIT_MASK_IN_UNIT(BITS_PER_UNIT + 5), BIT(5));
}

TEST_CASE(bit_set_clear_test_single_and_cross_unit)
{
    reg_t bits[2] = { 0, 0 };

    bit_set(bits, 0);
    ASSERT(bit_test(bits, 0));
    ASSERT_EQ(bits[0], (reg_t)1);

    bit_set(bits, BITS_PER_UNIT - 1);
    ASSERT(bit_test(bits, BITS_PER_UNIT - 1));
    ASSERT_EQ(bits[0], (reg_t)1 | BIT(BITS_PER_UNIT - 1));

    bit_set(bits, BITS_PER_UNIT);
    ASSERT(bit_test(bits, BITS_PER_UNIT));
    ASSERT_EQ(bits[1], (reg_t)1);

    bit_clear(bits, 0);
    ASSERT(!bit_test(bits, 0));
    ASSERT_EQ(bits[0], BIT(BITS_PER_UNIT - 1));

    bit_clear(bits, BITS_PER_UNIT - 1);
    ASSERT(!bit_test(bits, BITS_PER_UNIT - 1));
    ASSERT_EQ(bits[0], (reg_t)0);

    bit_clear(bits, BITS_PER_UNIT);
    ASSERT(!bit_test(bits, BITS_PER_UNIT));
    ASSERT_EQ(bits[1], (reg_t)0);
}

TEST_CASE(bit_set_clear_test_atomic_variants)
{
    reg_t bits[2] = { 0, 0 };

    bit_set_atomic(bits, 3, MO_SEQ_CST);
    ASSERT(bit_test_atomic(bits, 3, MO_SEQ_CST));
    ASSERT_EQ(bits[0], BIT(3));

    bit_set_atomic(bits, BITS_PER_UNIT + 7, MO_ACQ_REL);
    ASSERT(bit_test_atomic(bits, BITS_PER_UNIT + 7, MO_ACQUIRE));
    ASSERT_EQ(bits[1], BIT(7));

    bit_clear_atomic(bits, 3, MO_RELEASE);
    ASSERT(!bit_test_atomic(bits, 3, MO_RELAXED));
    ASSERT_EQ(bits[0], (reg_t)0);

    bit_clear_atomic(bits, BITS_PER_UNIT + 7, MO_SEQ_CST);
    ASSERT(!bit_test_atomic(bits, BITS_PER_UNIT + 7, MO_SEQ_CST));
    ASSERT_EQ(bits[1], (reg_t)0);
}
