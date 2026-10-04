#pragma once

#include <common/helpers.h>

#define COMPARE(x, y, op) ((x) op (y) ? (x) : (y))

#define RUNTIME_COMPARE(x, y, x_name, y_name, op) ({ \
    typeof(x) x_name = x;                            \
    typeof(y) y_name = y;                            \
    COMPARE(x_name, y_name, op);                     \
})

#define DO_COMPARE(x, y, op)                                            \
    CHOOSE_EXPR(                                                        \
        IS_CONSTEXPR(x) && IS_CONSTEXPR(y),                             \
        COMPARE(x, y, op),                                              \
        RUNTIME_COMPARE(                                                \
            x, y, CONCAT(ux, __COUNTER__), CONCAT(uy, __COUNTER__), op) \
        )

#define MIN(x, y) DO_COMPARE(x, y, <)
#define MAX(x, y) DO_COMPARE(x, y, >)

#define CLAMP_TYPE(value, low, high) typeof((value) + (low) + (high))

#define CLAMP_SIGNS_MATCH(value, low, high)           \
    ((IS_SIGNED_TYPE(value) && IS_SIGNED_TYPE(low) && \
      IS_SIGNED_TYPE(high)) ||                        \
     (IS_UNSIGNED_OR_NON_NEGATIVE_CONSTANT(value) &&  \
      IS_UNSIGNED_OR_NON_NEGATIVE_CONSTANT(low) &&    \
      IS_UNSIGNED_OR_NON_NEGATIVE_CONSTANT(high)))

#define CLAMP_LIMIT(x, low, high) \
    CHOOSE_EXPR(IS_CONSTEXPR(low) && IS_CONSTEXPR(high), (x), 0)

#define CLAMP_CHECKS(value, low, high) (                             \
    EMBED_STATIC_ASSERT(                                             \
        CLAMP_SIGNS_MATCH(value, low, high),                         \
        "CLAMP() arguments must be all signed or all unsigned"       \
    ) +                                                              \
    EMBED_STATIC_ASSERT(                                             \
        CLAMP_LIMIT(low, low, high) <= CLAMP_LIMIT(high, low, high), \
        "CLAMP() low limit must not be above the high limit"         \
    ))

#define CLAMP_COMPARE(value, low, high) \
    COMPARE(COMPARE(value, low, >), high, <)

#define RUNTIME_CLAMP(value, low, high, uv, ul, uh) ({ \
    CLAMP_TYPE(value, low, high) uv = (value);         \
    CLAMP_TYPE(value, low, high) ul = (low);           \
    CLAMP_TYPE(value, low, high) uh = (high);          \
                                                       \
    (void)CLAMP_CHECKS(value, low, high);              \
    CLAMP_COMPARE(uv, ul, uh);                         \
})

#define DO_CLAMP(value, low, high)                                           \
    CHOOSE_EXPR(                                                             \
        IS_CONSTEXPR(value) && IS_CONSTEXPR(low) && IS_CONSTEXPR(high),      \
        (CLAMP_TYPE(value, low, high))(                                      \
            CLAMP_CHECKS(value, low, high) + CLAMP_COMPARE(value, low, high) \
        ),                                                                   \
        RUNTIME_CLAMP(                                                       \
            value, low, high, CONCAT(uv, __COUNTER__),                       \
            CONCAT(ul, __COUNTER__), CONCAT(uh, __COUNTER__)                 \
        )                                                                    \
    )

// Clamp a 'value' to be between [low, high]
#define CLAMP(value, low, high) DO_CLAMP(value, low, high)

// Add 0u to keep u8 or a u16 from being promoted to a signed int
#define BOUNDS_TYPE(value, begin, end) typeof((value) + (begin) + (end) + 0u)

#define RUNTIME_WITHIN_BOUNDS(value, begin, end, op, uv, ub, ue) ({ \
    BOUNDS_TYPE(value, begin, end) uv = (value);                    \
    BOUNDS_TYPE(value, begin, end) ub = (begin);                    \
    BOUNDS_TYPE(value, begin, end) ue = (end);                      \
                                                                    \
    STATIC_ASSERT(                                                  \
        IS_UNSIGNED_TYPE(value) && IS_UNSIGNED_TYPE(begin) &&       \
        IS_UNSIGNED_TYPE(end), "bounds must be unsigned"            \
    );                                                              \
    uv - ub op ue - ub;                                             \
})

#define DO_WITHIN_BOUNDS(value, begin, end, op)          \
    RUNTIME_WITHIN_BOUNDS(                               \
        value, begin, end, op, CONCAT(uv, __COUNTER__),  \
        CONCAT(ub, __COUNTER__), CONCAT(ue, __COUNTER__) \
    )

/*
 * True if 'value' is within [begin, end). Every argument must be unsigned and
 * is evaluated once.
 */
#define within_bounds(value, begin, end) \
    DO_WITHIN_BOUNDS(value, begin, end, <)

/*
 * True if 'value' is within [begin, end]. Every argument must be unsigned and
 * is evaluated once.
 */
#define within_bounds_inclusive(value, first, last) \
    DO_WITHIN_BOUNDS(value, first, last, <=)
