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

#define this_cpu_add_8(val, x) (X86_APPLY_SEG_GS(val) += (x))
#define this_cpu_add_16(val, x) (X86_APPLY_SEG_GS(val) += (x))
#define this_cpu_add_32(val, x) (X86_APPLY_SEG_GS(val) += (x))
#define this_cpu_add_64(val, x) (X86_APPLY_SEG_GS(val) += (x))

#define this_cpu_sub_8(val, x) (X86_APPLY_SEG_GS(val) -= (x))
#define this_cpu_sub_16(val, x) (X86_APPLY_SEG_GS(val) -= (x))
#define this_cpu_sub_32(val, x) (X86_APPLY_SEG_GS(val) -= (x))
#define this_cpu_sub_64(val, x) (X86_APPLY_SEG_GS(val) -= (x))
