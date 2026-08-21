#include <smp.h>

u32 g_num_present_cpus = 0;
u32 g_num_online_cpus = 0;

struct cpu_mask g_online_cpus;
