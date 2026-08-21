#pragma once

#include <common/attributes.h>

#include <linker.h>
#include <smp.h>
#include <per_cpu_decls.h>

#include <arch/per_cpu.h>

#define per_cpu_ptr(ptr, cpu_number) \
    PTR_ADD_HIDE_UB(ptr, g_per_cpu_offset[cpu_number])

#define per_cpu(var, cpu_number) (*(per_cpu_ptr(&(var), cpu_number)))

#define this_cpu_ptr(ptr) per_cpu_ptr(ptr, unstable_cpu_id())

#define BUILD_BUG_ON_BAD_CPU_OP(var, op)                      \
    BUILD_BUG_ON_WITH_MSG(                                    \
        sizeof(var) != 1 &&                                   \
        sizeof(var) != 2 &&                                   \
        sizeof(var) != 4 &&                                   \
        sizeof(var) != 8,                                     \
        "this_cpu_" TO_STR(op) " only supports 1/2/4/8 byte " \
        "accesses, see this_cpu_ptr() for other types"        \
    )

#define DISPATCH_CPU_READ_OP(var, op) ({     \
    typeof(var) cread_ret;                   \
    BUILD_BUG_ON_BAD_CPU_OP(var, op);        \
                                             \
    switch (sizeof(var)) {                   \
    case 1:                                  \
        cread_ret = this_cpu_##op##_8(var);  \
        break;                               \
    case 2:                                  \
        cread_ret = this_cpu_##op##_16(var); \
        break;                               \
    case 4:                                  \
        cread_ret = this_cpu_##op##_32(var); \
        break;                               \
    case 8:                                  \
        cread_ret = this_cpu_##op##_64(var); \
        break;                               \
    }                                        \
    cread_ret;                               \
})

#define DISPATCH_CPU_WRITE_OP(var, op, x) ({ \
    BUILD_BUG_ON_BAD_CPU_OP(var, op);        \
                                             \
    switch (sizeof(var)) {                   \
    case 1:                                  \
        this_cpu_##op##_8(var, x);           \
        break;                               \
    case 2:                                  \
        this_cpu_##op##_16(var, x);          \
        break;                               \
    case 4:                                  \
        this_cpu_##op##_32(var, x);          \
        break;                               \
    case 8:                                  \
        this_cpu_##op##_64(var, x);          \
        break;                               \
    }                                        \
})

#define this_cpu_read(var) DISPATCH_CPU_READ_OP(var, read)
#define this_cpu_write(var, x) DISPATCH_CPU_WRITE_OP(var, write, x)

#define this_cpu_add(var, x) DISPATCH_CPU_WRITE_OP(var, add, x)
#define this_cpu_sub(var, x) DISPATCH_CPU_WRITE_OP(var, sub, x)

#define this_cpu_inc(var) this_cpu_add(var, 1)
#define this_cpu_dec(var) this_cpu_sub(var, 1)
