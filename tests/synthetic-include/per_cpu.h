#pragma once

#include <common/types.h>
#include <common/helpers.h>

/*
 * Usermode test stand-in for the kernel per-CPU machinery. The test suite runs
 * single-threaded on one "CPU", so a per-CPU variable is just a plain global
 * and this_cpu_read/write degrade to a direct access. Remote access through
 * per_cpu_ptr() keeps the kernel's offset table math, with the tests providing
 * the table and its backing storage.
 */

extern ptr_t g_per_cpu_offset[];

#define MAKE_PER_CPU(prefix, type, var) prefix type var
#define DEFINE_PER_CPU(type, var) type var
#define DECLARE_PER_CPU(type, var) extern type var

#define this_cpu_read(var) (var)
#define this_cpu_write(var, x) ((void)((var) = (x)))

#define this_cpu_add(var, x) ((void)((var) += (x)))
#define this_cpu_sub(var, x) ((void)((var) -= (x)))

#define this_cpu_inc(var) this_cpu_add(var, 1)
#define this_cpu_dec(var) this_cpu_sub(var, 1)

#define per_cpu_ptr(ptr, cpu_number) \
    PTR_ADD_HIDE_UB(ptr, g_per_cpu_offset[cpu_number])
