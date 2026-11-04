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
