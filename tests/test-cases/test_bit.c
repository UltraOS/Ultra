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

TEST_CASE(make_bitmap_macro)
{
    // Test that MAKE_BITMAP allocates correct number of units
    MAKE_BITMAP(bits_small, 1);
    MAKE_BITMAP(bits_exact, BITS_PER_UNIT);
    MAKE_BITMAP(bits_large, BITS_PER_UNIT * 3 + 5);

    ASSERT_EQ(sizeof(bits_small), sizeof(reg_t) * 1);
    ASSERT_EQ(sizeof(bits_exact), sizeof(reg_t) * 1);
    ASSERT_EQ(sizeof(bits_large), sizeof(reg_t) * 4);
}

TEST_CASE(bit_find_first_and_next)
{
    MAKE_BITMAP(bits, BITS_PER_UNIT * 3);

    /*
     * Initialize bitmap to all zeros and test finding set/zero bits.
     * We expect find_first_set_bit to return the number of bits when
     * no bits are set.
     */
    bits[0] = 0;
    bits[1] = 0;
    bits[2] = 0;

    ASSERT_EQ(find_first_set_bit(bits, BITS_PER_UNIT * 3), BITS_PER_UNIT * 3);
    ASSERT_EQ(find_first_set_bit(bits, 0), 0);

    ASSERT_EQ(find_first_zero_bit(bits, BITS_PER_UNIT * 3), 0);

    // Set some bits across different units
    bit_set(bits, 5);
    bit_set(bits, BITS_PER_UNIT + 10);
    bit_set(bits, BITS_PER_UNIT * 2 + 15);

    ASSERT_EQ(find_first_set_bit(bits, BITS_PER_UNIT * 3), 5);

    // Test find_next_set_bit with various start indices (inclusive search)
    ASSERT_EQ(find_next_set_bit(bits, BITS_PER_UNIT * 3, 0), 5);
    ASSERT_EQ(find_next_set_bit(bits, BITS_PER_UNIT * 3, 5), 5);
    
    ASSERT_EQ(find_next_set_bit(bits, BITS_PER_UNIT * 3, 6),
              BITS_PER_UNIT + 10);
    ASSERT_EQ(find_next_set_bit(bits, BITS_PER_UNIT * 3, BITS_PER_UNIT + 10),
              BITS_PER_UNIT + 10);
              
    ASSERT_EQ(find_next_set_bit(bits, BITS_PER_UNIT * 3, BITS_PER_UNIT + 11),
              BITS_PER_UNIT * 2 + 15);
    ASSERT_EQ(find_next_set_bit(bits, BITS_PER_UNIT * 3,
                                BITS_PER_UNIT * 2 + 15),
              BITS_PER_UNIT * 2 + 15);
              
    // Test behavior when starting after the last set bit
    ASSERT_EQ(find_next_set_bit(bits, BITS_PER_UNIT * 3,
                                BITS_PER_UNIT * 2 + 16),
              BITS_PER_UNIT * 3);

    // Test out of bounds start index
    ASSERT_EQ(find_next_set_bit(bits, BITS_PER_UNIT * 3,
                                BITS_PER_UNIT * 3),
              BITS_PER_UNIT * 3);
    ASSERT_EQ(find_next_set_bit(bits, BITS_PER_UNIT * 3,
                                BITS_PER_UNIT * 3 + 10),
              BITS_PER_UNIT * 3);

    /*
     * Initialize bitmap to all ones and test finding zero bits.
     * We expect find_first_zero_bit to return the number of bits when
     * no zero bits are present.
     */
    bits[0] = UNSIGNED_MAX(bits[0]);
    bits[1] = UNSIGNED_MAX(bits[1]);
    bits[2] = UNSIGNED_MAX(bits[2]);

    ASSERT_EQ(find_first_zero_bit(bits, BITS_PER_UNIT * 3), BITS_PER_UNIT * 3);

    // Clear some bits across different units
    bit_clear(bits, 7);
    bit_clear(bits, BITS_PER_UNIT + 12);
    bit_clear(bits, BITS_PER_UNIT * 2 + 17);

    ASSERT_EQ(find_first_zero_bit(bits, BITS_PER_UNIT * 3), 7);

    // Test find_next_zero_bit with various start indices (inclusive search)
    ASSERT_EQ(find_next_zero_bit(bits, BITS_PER_UNIT * 3, 0), 7);
    ASSERT_EQ(find_next_zero_bit(bits, BITS_PER_UNIT * 3, 7), 7);
    
    ASSERT_EQ(find_next_zero_bit(bits, BITS_PER_UNIT * 3, 8),
              BITS_PER_UNIT + 12);
    ASSERT_EQ(find_next_zero_bit(bits, BITS_PER_UNIT * 3, BITS_PER_UNIT + 12),
              BITS_PER_UNIT + 12);
              
    ASSERT_EQ(find_next_zero_bit(bits, BITS_PER_UNIT * 3, BITS_PER_UNIT + 13),
              BITS_PER_UNIT * 2 + 17);
    ASSERT_EQ(find_next_zero_bit(bits, BITS_PER_UNIT * 3,
                                 BITS_PER_UNIT * 2 + 17),
              BITS_PER_UNIT * 2 + 17);
              
    // Test behavior when starting after the last zero bit
    ASSERT_EQ(find_next_zero_bit(bits, BITS_PER_UNIT * 3,
                                 BITS_PER_UNIT * 2 + 18),
              BITS_PER_UNIT * 3);
}

TEST_CASE(bit_array_count_set_basic)
{
    MAKE_BITMAP(bits, BITS_PER_UNIT * 3) = { 0 };

    ASSERT_EQ(bit_array_count_set(bits, BITS_PER_UNIT * 3), 0);

    bit_set(bits, 0);
    bit_set(bits, 7);
    bit_set(bits, BITS_PER_UNIT + 1);
    bit_set(bits, BITS_PER_UNIT * 2 + 33);
    ASSERT_EQ(bit_array_count_set(bits, BITS_PER_UNIT * 3), 4);

    bits[0] = UNSIGNED_MAX(bits[0]);
    bits[1] = UNSIGNED_MAX(bits[1]);
    bits[2] = UNSIGNED_MAX(bits[2]);
    ASSERT_EQ(bit_array_count_set(bits, BITS_PER_UNIT * 3),
              BITS_PER_UNIT * 3);
}

TEST_CASE(bit_array_count_set_partial_tail)
{
    MAKE_BITMAP(bits, BITS_PER_UNIT + 5) = { 0 };

    /*
     * Set bits beyond num_bits in the last unit, they must not be
     * counted.
     */
    bits[1] = UNSIGNED_MAX(bits[1]);
    ASSERT_EQ(bit_array_count_set(bits, BITS_PER_UNIT + 5), 5);

    bits[0] = UNSIGNED_MAX(bits[0]);
    ASSERT_EQ(bit_array_count_set(bits, BITS_PER_UNIT + 5),
              BITS_PER_UNIT + 5);
}

TEST_CASE(bit_array_and_basic)
{
    MAKE_BITMAP(lhs, BITS_PER_UNIT * 2) = { 0 };
    MAKE_BITMAP(rhs, BITS_PER_UNIT * 2) = { 0 };
    MAKE_BITMAP(dst, BITS_PER_UNIT * 2) = { 0 };

    bit_set(lhs, 3);
    bit_set(lhs, 10);
    bit_set(lhs, BITS_PER_UNIT + 4);

    bit_set(rhs, 10);
    bit_set(rhs, BITS_PER_UNIT + 4);
    bit_set(rhs, BITS_PER_UNIT + 9);

    bit_array_and(dst, lhs, rhs, BITS_PER_UNIT * 2);

    ASSERT(!bit_test(dst, 3));
    ASSERT(bit_test(dst, 10));
    ASSERT(bit_test(dst, BITS_PER_UNIT + 4));
    ASSERT(!bit_test(dst, BITS_PER_UNIT + 9));
    ASSERT_EQ(bit_array_count_set(dst, BITS_PER_UNIT * 2), 2);
}
