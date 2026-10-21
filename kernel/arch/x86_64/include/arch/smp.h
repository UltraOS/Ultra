#pragma once

#include <common/helpers.h>
#include <per_cpu_decls.h>
#include <arch/detail/per_cpu.h>

DECLARE_PER_CPU(cpu_t, g_this_cpu_id);

BUILD_BUG_ON(sizeof(cpu_t) != sizeof(u32));
#define unstable_cpu_id() this_cpu_read_32(g_this_cpu_id)
