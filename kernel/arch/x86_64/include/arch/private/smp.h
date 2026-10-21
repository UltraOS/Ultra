#pragma once

#include <arch/private/apic.h>
#include <per_cpu.h>

DECLARE_PER_CPU(apic_id_t, g_this_cpu_apic_id);

void setup_smp_topology(void);
