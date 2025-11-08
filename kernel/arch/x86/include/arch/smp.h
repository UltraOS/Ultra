#pragma once

#include <per_cpu_decls.h>
#include <arch/per_cpu.h>

DECLARE_PER_CPU(u32, g_this_cpu_id);

#define unstable_cpu_id() this_cpu_read_32(g_this_cpu_id)
