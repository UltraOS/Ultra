#pragma once

#include <per_cpu.h>

DECLARE_PER_CPU(u32, g_this_cpu_apic_id);

void setup_smp_topology(void);
