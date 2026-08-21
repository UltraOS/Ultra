#pragma once

#include <common/bit.h>
#include <common/error.h>
#include <common/string.h>
#include <config.h>

struct cpu_mask {
    MAKE_BITMAP(bits, ULTRA_MAX_CPUS);
};

/*
 * The number of bits every mask operation is bounded by. Out-of-line
 * handles are allocated with only this many bits of storage, so no
 * operation may ever touch bits past it.
 */
#if IS_ENABLED(CPU_MASK_OUT_OF_LINE)
extern u32 g_num_present_cpus;
#define CPU_MASK_NUM_BITS g_num_present_cpus
#else
#define CPU_MASK_NUM_BITS ULTRA_MAX_CPUS
#endif

#define CPU_MASK_NUM_BYTES \
    (NUM_UNITS_FOR_BITS(CPU_MASK_NUM_BITS) * sizeof(reg_t))

static inline void cpu_mask_set(struct cpu_mask *mask, u32 cpu)
{
    bit_set(mask->bits, cpu);
}

static inline void cpu_mask_clear(struct cpu_mask *mask, u32 cpu)
{
    bit_clear(mask->bits, cpu);
}

static inline bool cpu_mask_test(const struct cpu_mask *mask, u32 cpu)
{
    return bit_test(mask->bits, cpu);
}

static inline void cpu_mask_clear_all(struct cpu_mask *mask)
{
    memzero(mask->bits, CPU_MASK_NUM_BYTES);
}

static inline void cpu_mask_copy(
    struct cpu_mask *dst, const struct cpu_mask *src
)
{
    memcpy(dst->bits, src->bits, CPU_MASK_NUM_BYTES);
}

static inline void cpu_mask_and(
    struct cpu_mask *dst, const struct cpu_mask *lhs,
    const struct cpu_mask *rhs
)
{
    bit_array_and(dst->bits, lhs->bits, rhs->bits, CPU_MASK_NUM_BITS);
}

static inline u32 cpu_mask_weight(const struct cpu_mask *mask)
{
    return bit_array_count_set(mask->bits, CPU_MASK_NUM_BITS);
}

// Returns CPU_MASK_NUM_BITS if no set bits are present
static inline u32 cpu_mask_first(const struct cpu_mask *mask)
{
    return find_first_set_bit(mask->bits, CPU_MASK_NUM_BITS);
}

// Returns CPU_MASK_NUM_BITS if no set bits are present after 'prev'
static inline u32 cpu_mask_next(const struct cpu_mask *mask, u32 prev)
{
    return find_next_set_bit(mask->bits, CPU_MASK_NUM_BITS, prev + 1);
}

static inline bool cpu_mask_empty(const struct cpu_mask *mask)
{
    return cpu_mask_first(mask) == CPU_MASK_NUM_BITS;
}

#define for_each_cpu_in(cpu, mask)     \
    for (cpu = cpu_mask_first(mask);   \
         cpu < CPU_MASK_NUM_BITS;      \
         cpu = cpu_mask_next(mask, cpu))

/*
 * An owned mask for locals, usable as a plain struct cpu_mask pointer
 * once allocated:
 *
 *     cpu_mask_handle mask;
 *
 *     ret = cpu_mask_handle_alloc(&mask);
 *     if (is_error(ret))
 *         return ret;
 *     ...
 *     cpu_mask_handle_free(mask);
 *
 * With CPU_MASK_OUT_OF_LINE the storage lives on the heap sized by
 * the detected CPU count, so handles may only be allocated once that
 * count is known. Handles always come back zeroed. Never copy any
 * mask with '*dst = *src', only via cpu_mask_copy(), a handle may be
 * smaller than the full structure.
 */
#if IS_ENABLED(CPU_MASK_OUT_OF_LINE)

typedef struct cpu_mask *cpu_mask_handle;

error_t cpu_mask_handle_alloc(cpu_mask_handle *out_mask);
void cpu_mask_handle_free(cpu_mask_handle mask);

#else

typedef struct cpu_mask cpu_mask_handle[1];

static inline error_t cpu_mask_handle_alloc(cpu_mask_handle *out_mask)
{
    cpu_mask_clear_all(*out_mask);
    return EOK;
}

static inline void cpu_mask_handle_free(cpu_mask_handle mask)
{
    UNREFERENCED_PARAMETER(mask);
}

#endif
