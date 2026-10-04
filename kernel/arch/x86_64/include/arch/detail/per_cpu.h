#pragma once

#include <common/types.h>

/*
 * Cast to ptr_t first to prevent GCC from complaining:
 *      cast to '__seg_gs' address space pointer from disjoint generic address
 *      space pointer [-Werror]
 */
#define X86_APPLY_SEG_GS(val) (*((typeof(val) __seg_gs*)((ptr_t)(&val))))

#define this_cpu_read_8(val) X86_APPLY_SEG_GS(val)
#define this_cpu_read_16(val) X86_APPLY_SEG_GS(val)
#define this_cpu_read_32(val) X86_APPLY_SEG_GS(val)
#define this_cpu_read_64(val) X86_APPLY_SEG_GS(val)

#define this_cpu_write_8(val, x) (X86_APPLY_SEG_GS(val) = (x))
#define this_cpu_write_16(val, x) (X86_APPLY_SEG_GS(val) = (x))
#define this_cpu_write_32(val, x) (X86_APPLY_SEG_GS(val) = (x))
#define this_cpu_write_64(val, x) (X86_APPLY_SEG_GS(val) = (x))

/*
 * This uses inline assembly instead of raw C with __seg_gs because debug
 * builds at O0 make some of these operations RMW and thus not compatible
 * with this_cpu_* semantics, which are assumed to be atomic.
 */
#define X86_THIS_CPU_MODIFY(insn, constraint, type, val, x) \
    asm volatile(                                           \
        insn " %[value], %%gs:%[var]"                       \
        : [var] "+m" (val)                                  \
        : [value] constraint ((type)(x))                    \
        : "cc"                                              \
    )

#define this_cpu_add_8(val, x) X86_THIS_CPU_MODIFY("addb", "qi", u8, val, x)
#define this_cpu_add_16(val, x) X86_THIS_CPU_MODIFY("addw", "ri", u16, val, x)
#define this_cpu_add_32(val, x) X86_THIS_CPU_MODIFY("addl", "ri", u32, val, x)
#define this_cpu_add_64(val, x) X86_THIS_CPU_MODIFY("addq", "re", u64, val, x)

#define this_cpu_sub_8(val, x) X86_THIS_CPU_MODIFY("subb", "qi", u8, val, x)
#define this_cpu_sub_16(val, x) X86_THIS_CPU_MODIFY("subw", "ri", u16, val, x)
#define this_cpu_sub_32(val, x) X86_THIS_CPU_MODIFY("subl", "ri", u32, val, x)
#define this_cpu_sub_64(val, x) X86_THIS_CPU_MODIFY("subq", "re", u64, val, x)

#define this_cpu_and_8(val, x) X86_THIS_CPU_MODIFY("andb", "qi", u8, val, x)
#define this_cpu_and_16(val, x) X86_THIS_CPU_MODIFY("andw", "ri", u16, val, x)
#define this_cpu_and_32(val, x) X86_THIS_CPU_MODIFY("andl", "ri", u32, val, x)
#define this_cpu_and_64(val, x) X86_THIS_CPU_MODIFY("andq", "re", u64, val, x)

#define this_cpu_or_8(val, x) X86_THIS_CPU_MODIFY("orb", "qi", u8, val, x)
#define this_cpu_or_16(val, x) X86_THIS_CPU_MODIFY("orw", "ri", u16, val, x)
#define this_cpu_or_32(val, x) X86_THIS_CPU_MODIFY("orl", "ri", u32, val, x)
#define this_cpu_or_64(val, x) X86_THIS_CPU_MODIFY("orq", "re", u64, val, x)

/*
 * Don't use xchg here, which implies the lock prefix when dealing with a
 * memory operand. We don't need such semantics for per-cpu private variables,
 * so a cmpxchg loop suffices.
 */
#define X86_THIS_CPU_XCHG(suffix, constraint, type, val, x) ({ \
    type xchg_old, xchg_new;                                   \
                                                               \
    xchg_new = (type)(ptr_t)(typeof(val))(x);                  \
    asm volatile(                                              \
        "mov" suffix " %%gs:%[var], %[old]\n"                  \
        "1:\n"                                                 \
        "cmpxchg" suffix " %[new], %%gs:%[var]\n"              \
        "jnz 1b"                                               \
        : [old] "=&a" (xchg_old), [var] "+m" (val)             \
        : [new] constraint (xchg_new)                          \
        : "cc"                                                 \
    );                                                         \
                                                               \
    (typeof(val))(ptr_t)xchg_old;                              \
})

#define this_cpu_xchg_8(val, x)             \
    X86_THIS_CPU_XCHG("b", "q", u8, val, x)
#define this_cpu_xchg_16(val, x)             \
    X86_THIS_CPU_XCHG("w", "r", u16, val, x)
#define this_cpu_xchg_32(val, x)             \
    X86_THIS_CPU_XCHG("l", "r", u32, val, x)
#define this_cpu_xchg_64(val, x)             \
    X86_THIS_CPU_XCHG("q", "r", u64, val, x)

#define X86_THIS_CPU_CMPXCHG(                                                \
    suffix, constraint, type, val, expected, desired                         \
) ({                                                                         \
    type cmpxchg_old, cmpxchg_new;                                           \
    bool cmpxchg_done;                                                       \
                                                                             \
    cmpxchg_old = (type)(ptr_t)(*(expected));                                \
    cmpxchg_new = (type)(ptr_t)(typeof(val))(desired);                       \
    asm volatile(                                                            \
        "cmpxchg" suffix " %[new], %%gs:%[var]"                              \
        : "=@ccz" (cmpxchg_done), [old] "+a" (cmpxchg_old), [var] "+m" (val) \
        : [new] constraint (cmpxchg_new)                                     \
    );                                                                       \
                                                                             \
    if (!cmpxchg_done)                                                       \
        *(expected) = (typeof(val))(ptr_t)cmpxchg_old;                       \
                                                                             \
    cmpxchg_done;                                                            \
})

#define this_cpu_cmpxchg_8(val, expected, desired)             \
    X86_THIS_CPU_CMPXCHG("b", "q", u8, val, expected, desired)
#define this_cpu_cmpxchg_16(val, expected, desired)             \
    X86_THIS_CPU_CMPXCHG("w", "r", u16, val, expected, desired)
#define this_cpu_cmpxchg_32(val, expected, desired)             \
    X86_THIS_CPU_CMPXCHG("l", "r", u32, val, expected, desired)
#define this_cpu_cmpxchg_64(val, expected, desired)             \
    X86_THIS_CPU_CMPXCHG("q", "r", u64, val, expected, desired)
