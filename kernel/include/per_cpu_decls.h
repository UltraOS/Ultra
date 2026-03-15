#pragma once

#include <common/attributes.h>
#include <common/types.h>
#include <linker.h>

#include <arch/per_cpu.h>

#ifndef ARCH_PER_CPU_ATTRIBUTES
    #define ARCH_PER_CPU_ATTRIBUTES
#endif

extern ptr_t g_per_cpu_offset[ULTRA_MAX_CPUS];
extern virt_addr_t g_per_cpu_base;

#define MAKE_PER_CPU(prefix, type, var) \
    prefix SECTION(PER_CPU_SECTION) ARCH_PER_CPU_ATTRIBUTES typeof(type) var

#define DEFINE_PER_CPU(type, var) MAKE_PER_CPU(, type, var)
#define DECLARE_PER_CPU(type, var) MAKE_PER_CPU(extern, type, var)
