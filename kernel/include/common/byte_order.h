#pragma once

#include <common/types.h>
#include <common/attributes.h>

#if !HAS_BUILTIN(__builtin_bswap16) || !HAS_BUILTIN(__builtin_bswap32) || \
    !HAS_BUILTIN(__builtin_bswap64)
#error No builtin support for bswap on this compiler!
#endif

#define MAKE_ENDIAN_TYPE(type, width)                                   \
    typedef struct type##width {                                        \
        u##width dont_touch_me_directly_use_byte_order_helpers_instead; \
    } type##width

MAKE_ENDIAN_TYPE(le, 8);
MAKE_ENDIAN_TYPE(le, 16);
MAKE_ENDIAN_TYPE(le, 32);
MAKE_ENDIAN_TYPE(le, 64);

MAKE_ENDIAN_TYPE(be, 8);
MAKE_ENDIAN_TYPE(be, 16);
MAKE_ENDIAN_TYPE(be, 32);
MAKE_ENDIAN_TYPE(be, 64);

#define MAKE_ENDIAN_CONVERSION(type, width)                           \
    static inline u##width type##width##_to_cpu(type##width val)      \
    {                                                                 \
        return __builtin_bswap##width(                                \
            val.dont_touch_me_directly_use_byte_order_helpers_instead \
        );                                                            \
    }                                                                 \
                                                                      \
    static inline type##width cpu_to_##type##width(u##width val)      \
    {                                                                 \
        return (type##width) { __builtin_bswap##width(val) };         \
    }

#define MAKE_NO_OP_ENDIAN_CONVERSION(type, width)                         \
    static inline u##width type##width##_to_cpu(type##width val)          \
    {                                                                     \
        return val.dont_touch_me_directly_use_byte_order_helpers_instead; \
    }                                                                     \
                                                                          \
    static inline type##width cpu_to_##type##width(u##width val)          \
    {                                                                     \
        return (type##width) { val };                                     \
    }

#ifdef ULTRA_ARCH_LITTLE_ENDIAN
    MAKE_ENDIAN_CONVERSION(be, 16)
    MAKE_ENDIAN_CONVERSION(be, 32)
    MAKE_ENDIAN_CONVERSION(be, 64)

    MAKE_NO_OP_ENDIAN_CONVERSION(le, 16)
    MAKE_NO_OP_ENDIAN_CONVERSION(le, 32)
    MAKE_NO_OP_ENDIAN_CONVERSION(le, 64)
#else
    MAKE_ENDIAN_CONVERSION(le, 16)
    MAKE_ENDIAN_CONVERSION(le, 32)
    MAKE_ENDIAN_CONVERSION(le, 64)

    MAKE_NO_OP_ENDIAN_CONVERSION(be, 16)
    MAKE_NO_OP_ENDIAN_CONVERSION(be, 32)
    MAKE_NO_OP_ENDIAN_CONVERSION(be, 64)
#endif

