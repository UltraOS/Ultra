#pragma once

#include <cpu_mask.h>

extern cpu_t g_num_present_cpus;
extern struct cpu_mask g_online_cpus;

#define for_each_online_cpu(cpu) for_each_cpu_in(cpu, &g_online_cpus)

#define for_each_present_cpu(cpu)                        \
    for ((cpu) = 0; (cpu) < g_num_present_cpus; (cpu)++)
