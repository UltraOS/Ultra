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
