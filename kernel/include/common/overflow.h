#pragma once

#include <common/attributes.h>
#include <common/helpers.h>

static ALWAYS_INLINE MUST_CHECK bool must_check_overflow(bool overflow)
{
    return unlikely(overflow);
}

/*
 * Compute 'a + b', 'a - b' or 'a * b' and store the result in '*result'.
 * Returns true if there was an overflow, false otherwise.
 */
#define check_add_overflow(a, b, result) \
    must_check_overflow(__builtin_add_overflow(a, b, result))
#define check_sub_overflow(a, b, result) \
    must_check_overflow(__builtin_sub_overflow(a, b, result))
#define check_mul_overflow(a, b, result) \
    must_check_overflow(__builtin_mul_overflow(a, b, result))

/*
 * Compute 'a - b', or 0 if 'b' is greater than 'a'. The result has the type
 * of 'a'. Both must be unsigned, except that 'b' may also be a non-negative
 * constant.
 */
#define saturating_sub(a, b) ({                                         \
    typeof(a) saturating_sub_result;                                    \
                                                                        \
    STATIC_ASSERT(                                                      \
        IS_UNSIGNED_TYPE(a) && IS_UNSIGNED_OR_NON_NEGATIVE_CONSTANT(b), \
        "saturating_sub() only supports unsigned numbers"               \
    );                                                                  \
                                                                        \
    if (check_sub_overflow(a, b, &saturating_sub_result))               \
        saturating_sub_result = 0;                                      \
                                                                        \
    saturating_sub_result;                                              \
})
