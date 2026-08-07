#pragma once

/*
 * Usermode test stand-in for the kernel per-CPU machinery. The test suite runs
 * single-threaded on one "CPU", so a per-CPU variable is just a plain global
 * and this_cpu_read/write degrade to a direct access.
 */

#define MAKE_PER_CPU(prefix, type, var) prefix type var
#define DEFINE_PER_CPU(type, var) type var
#define DECLARE_PER_CPU(type, var) extern type var

#define this_cpu_read(var) (var)
#define this_cpu_write(var, x) ((void)((var) = (x)))
