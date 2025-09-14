#pragma once

#include <common/attributes.h>
#include <arch/constants.h>

#if HAS_BUILTIN(__builtin_align_up)
    #define ALIGN_UP(x, val) __builtin_align_up(x, val)
#else
    #define ALIGN_UP_MASK(x, mask) (((x) + (mask)) & ~(mask))
    #define ALIGN_UP(x, val) ALIGN_UP_MASK(x, (typeof(x))(val) - 1)
#endif

#if HAS_BUILTIN(__builtin_align_down)
    #define ALIGN_DOWN(x, val) __builtin_align_down(x, val)
#else
    #define ALIGN_DOWN_MASK(x, mask) ((x) & ~(mask))
    #define ALIGN_DOWN(x, val) ALIGN_DOWN_MASK(x, (typeof(x))(val) - 1)
#endif

#if HAS_BUILTIN(__builtin_is_aligned)
    #define IS_ALIGNED(x, val) __builtin_is_aligned(x, val)
#else
    #define IS_ALIGNED_MASK(x, mask) (((x) & (mask)) == 0)
    #define IS_ALIGNED(x, val) IS_ALIGNED_MASK(x, (typeof(x))(val) - 1)
#endif

#define PAGE_ROUND_UP(size)   ALIGN_UP(size, PAGE_SIZE)
#define PAGE_ROUND_DOWN(size) ALIGN_DOWN(size, PAGE_SIZE)

#define ALIGN_OF(x) _Alignof(x)
