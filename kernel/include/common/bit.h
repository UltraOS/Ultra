#pragma once

#include <common/types.h>
#include <common/atomic.h>
#include <common/helpers.h>

#define BITS_PER_BYTE CHAR_BIT
#define BITS_PER_TYPE(type) (sizeof(type) * BITS_PER_BYTE)

#define BIT_OF_TYPE(type, x) (((type)1) << (x))
#define BIT_U8(x) BIT_OF_TYPE(u8, (x))
#define BIT_U16(x) BIT_OF_TYPE(u16, (x))
#define BIT_U32(x) BIT_OF_TYPE(u32, (x))
#define BIT_U64(x) BIT_OF_TYPE(u64, (x))
#define BIT(x) BIT_OF_TYPE(reg_t, (x))

/*
 * Helpers below must use embedded static asserts instead of ({ }) to make the
 * compiler treat them as compile time constants.
 */
#define UNSIGNED_HALF_MAX(type) (                               \
    EMBED_STATIC_ASSERT(                                        \
        IS_UNSIGNED_TYPE(typeof(type)), "type must be unsigned" \
    ) + BIT(BITS_PER_TYPE(typeof(type)) - 1))

#define UNSIGNED_MAX(type) \
    ((typeof(type))(UNSIGNED_HALF_MAX(type) + (UNSIGNED_HALF_MAX(type) - 1)))

#define MAKE_BIT_MASK_OF_TYPE(end_bit, start_bit, type) ((type)                \
    STATIC_ASSERT_IF_CONSTEXPR((end_bit) > (start_bit), "incorrect bit order") \
    +  (((UNSIGNED_MAX(type) >> (BITS_PER_TYPE(type) - (end_bit) - 1))) &      \
        ((UNSIGNED_MAX(type) << (start_bit)))))

#define MAKE_BIT_MASK_U8(end_bit, start_bit) \
    MAKE_BIT_MASK_OF_TYPE(end_bit, start_bit, u8)
#define MAKE_BIT_MASK_U16(end_bit, start_bit) \
    MAKE_BIT_MASK_OF_TYPE(end_bit, start_bit, u16)
#define MAKE_BIT_MASK_U32(end_bit, start_bit) \
    MAKE_BIT_MASK_OF_TYPE(end_bit, start_bit, u32)
#define MAKE_BIT_MASK_U64(end_bit, start_bit) \
    MAKE_BIT_MASK_OF_TYPE(end_bit, start_bit, u64)
#define MAKE_BIT_MASK(end_bit, start_bit) \
    MAKE_BIT_MASK_OF_TYPE(end_bit, start_bit, reg_t)

#define BIT_FIELD_SHIFT(mask) (__builtin_ffsll(mask) - 1)

#define BIT_FIELD_VALIDATE_INPUT(mask, value)                                    \
    EMBED_STATIC_ASSERT(IS_UNSIGNED_TYPE(mask), "field mask must be unsigned") + \
    EMBED_STATIC_ASSERT(IS_CONSTEXPR(mask), "field mask must be a constant") +   \
    EMBED_STATIC_ASSERT(mask != 0, "field mask must be a non-zero value") +      \
    EMBED_STATIC_ASSERT(IS_POWER_OF_TWO(mask + BIT(BIT_FIELD_SHIFT(mask))),      \
                        "field mask must be contiguous") +                       \
    STATIC_ASSERT_IF_CONSTEXPR(                                                  \
        (~(mask >> BIT_FIELD_SHIFT(mask)) & (value)) == 0,                       \
        "value is larger than the field mask accepts"                            \
    )

#define BIT_FIELD_VALIDATE_DATA(data, mask)                              \
    EMBED_STATIC_ASSERT(UNSIGNED_MAX(data) >= mask,                      \
                        "data is too small to store the field specified")

/*
 * Read a bit field defined by 'mask' that is stored in 'data'.
 * The return value is a compile-time constant expression as long as 'data' is
 * also a constant expression.
 *
 * 'mask' must be an unsigned, bit-contiguous compile-time constant. Use either
 * BIT or MAKE_BIT_MASK to generate one.
 */
#define BIT_FIELD_READ(data, mask) ((typeof(mask))( \
    BIT_FIELD_VALIDATE_INPUT(mask, 0) +             \
    BIT_FIELD_VALIDATE_DATA(data, mask) +           \
    (((data) & (mask)) >> BIT_FIELD_SHIFT(mask))))

/*
 * Make a bit field defined by 'mask' out of the provided 'value'.
 * The return value is a compile-time constant expression as long as 'value' is
 * also a constant expression.
 *
 * 'mask' must be an unsigned, bit-contiguous compile-time constant. Use either
 * BIT or MAKE_BIT_MASK to generate one.
 */
#define BIT_FIELD_MAKE(mask, value) ((typeof(mask))( \
    BIT_FIELD_VALIDATE_INPUT(mask, value) +          \
    (((value) << BIT_FIELD_SHIFT(mask)) & (mask))))

/*
 * Set a bit field in 'dst' defined by 'mask' to 'value'
 *
 * 'mask' must be an unsigned, bit-contiguous compile-time constant. Use either
 * BIT or MAKE_BIT_MASK to generate one.
 */
#define BIT_FIELD_WRITE(dst, mask, value) ({       \
    (void)(BIT_FIELD_VALIDATE_INPUT(mask, value)); \
    (void)(BIT_FIELD_VALIDATE_DATA(dst, mask));    \
    (dst) &= ~(mask);                              \
    (dst) |= BIT_FIELD_MAKE(mask, value);          \
})

/*
 * Returns the maximum value that may be stored in the field defined by 'mask'.
 * The return value is a compile-time constant expression as long as 'value' is
 * also a constant expression.
 *
 * 'mask' must be an unsigned, bit-contiguous compile-time constant. Use either
 * BIT or MAKE_BIT_MASK to generate one.
 */
#define BIT_FIELD_MAX(mask) ((typeof(mask))( \
    BIT_FIELD_VALIDATE_INPUT(mask, 0) +      \
    ((mask) >> BIT_FIELD_SHIFT(mask))))

/*
 * Returns true if the provided 'value' may be stored in the field defined by
 * 'mask', false otherwise. The return value is a compile-time constant
 * expression as long as 'value' is also a constant expression.
 *
 * 'mask' must be an unsigned, bit-contiguous compile-time constant. Use either
 * BIT or MAKE_BIT_MASK to generate one.
 */
#define BIT_FIELD_FITS(mask, value) ((bool)(            \
    BIT_FIELD_VALIDATE_INPUT(mask, 0) +                 \
    ((~(mask >> BIT_FIELD_SHIFT(mask)) & (value)) == 0)))

/*
 * Bit helpers operate on "units" or arrays of "units", where each unit has
 * native architecture width, hence we use reg_t to refer to these units.
 */
#define BITS_PER_UNIT BITS_PER_TYPE(reg_t)
#define BIT_UNIT(x) ((x) / BITS_PER_UNIT)
#define BIT_INDEX(x) ((x) % BITS_PER_UNIT)
#define BIT_MASK_IN_UNIT(x) ((reg_t)1 << BIT_INDEX(x))

static inline void bit_set(reg_t *bit_array, reg_t bit)
{
    bit_array[BIT_UNIT(bit)] |= BIT_MASK_IN_UNIT(bit);
}

static inline void bit_set_atomic(
    reg_t *bit_array, reg_t bit, enum memory_order mo
)
{
    atomic_or_fetch(&bit_array[BIT_UNIT(bit)], BIT_MASK_IN_UNIT(bit), mo);
}

static inline void bit_clear(reg_t *bit_array, reg_t bit)
{
    bit_array[BIT_UNIT(bit)] &= ~BIT_MASK_IN_UNIT(bit);
}

static inline void bit_clear_atomic(
    reg_t *bit_array, reg_t bit, enum memory_order mo
)
{
    atomic_and_fetch(&bit_array[BIT_UNIT(bit)], ~BIT_MASK_IN_UNIT(bit), mo);
}

static inline bool bit_test(const reg_t *bit_array, reg_t bit)
{
    return bit_array[BIT_UNIT(bit)] & BIT_MASK_IN_UNIT(bit);
}

static inline bool bit_test_atomic(
    const reg_t *bit_array, reg_t bit, enum memory_order mo
)
{
    reg_t unit;

    unit = atomic_load_explicit(&bit_array[BIT_UNIT(bit)], mo);
    return unit & BIT_MASK_IN_UNIT(bit);
}
