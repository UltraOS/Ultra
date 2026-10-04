#pragma once

#include <common/types.h>
#include <common/helpers.h>
#include <common/atomic.h>
#include <common/bit.h>

/*
 * A count above MAX is broken. Once caught it is parked at SATURATED,
 * halfway into the broken range, so racing increments and decrements
 * can't bring it back to 0 or into the valid range. The object leaks
 * instead of being freed while in use.
 */
#define REFCOUNT32_MAX MAKE_BIT_MASK_U32(30, 0)
#define REFCOUNT32_SATURATED MAKE_BIT_MASK_U32(31, 30)

#define REFCOUNT64_MAX MAKE_BIT_MASK_U64(62, 0)
#define REFCOUNT64_SATURATED MAKE_BIT_MASK_U64(63, 62)

/*
 * At one increment per nanosecond, a 64-bit count takes 292 years to pass
 * MAX, so it is only checked for use-after-free.
 */
#define REFCOUNT_CAN_OVERFLOW(width) ((width) < 64)

enum refcount_op : u8 {
    REFCOUNT_OP_INC,
    REFCOUNT_OP_DEC,
};

/*
 * set() and read() are relaxed: the count is set before the object is
 * published, and a read is only a snapshot. dec_and_test() releases so
 * this owner's writes happen before the free, and acquires on the final
 * reference so the freeing thread sees every owner's writes.
 */
#define MAKE_REFCOUNT_CLASS(width)                                        \
    struct refcount##width {                                              \
        u##width value;                                                   \
    };                                                                    \
                                                                          \
    void refcount##width##_saturate(                                      \
        struct refcount##width *ref, u##width old, enum refcount_op op    \
    );                                                                    \
                                                                          \
    static inline void refcount##width##_set(                             \
        struct refcount##width *ref, u##width value                       \
    )                                                                     \
    {                                                                     \
        atomic_store_relaxed(&ref->value, value);                         \
    }                                                                     \
                                                                          \
    static inline u##width refcount##width##_read(                        \
        const struct refcount##width *ref                                 \
    )                                                                     \
    {                                                                     \
        return atomic_load_relaxed(&ref->value);                          \
    }                                                                     \
                                                                          \
    static inline void refcount##width##_inc(struct refcount##width *ref) \
    {                                                                     \
        u##width old;                                                     \
                                                                          \
        old = atomic_fetch_add(&ref->value, 1, MO_RELAXED);               \
        if (unlikely(old == 0 || (REFCOUNT_CAN_OVERFLOW(width) &&         \
                                  old >= REFCOUNT##width##_MAX)))         \
            refcount##width##_saturate(ref, old, REFCOUNT_OP_INC);        \
    }                                                                     \
                                                                          \
    static inline bool refcount##width##_dec_and_test(                    \
        struct refcount##width *ref                                       \
    )                                                                     \
    {                                                                     \
        u##width old;                                                     \
                                                                          \
        old = atomic_fetch_sub(&ref->value, 1, MO_RELEASE);               \
        if (old == 1) {                                                   \
            barrier_acquire();                                            \
            return true;                                                  \
        }                                                                 \
                                                                          \
        if (unlikely(old == 0 || (REFCOUNT_CAN_OVERFLOW(width) &&         \
                                  old > REFCOUNT##width##_MAX)))          \
            refcount##width##_saturate(ref, old, REFCOUNT_OP_DEC);        \
                                                                          \
        return false;                                                     \
    }

MAKE_REFCOUNT_CLASS(32)
MAKE_REFCOUNT_CLASS(64)

#define REFCOUNT_DISPATCH(ref, op) (_Generic(*(ref), \
    struct refcount32: refcount32_##op,              \
    struct refcount64: refcount64_##op))
