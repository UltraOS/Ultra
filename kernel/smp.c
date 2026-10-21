#include <smp.h>

cpu_t g_num_present_cpus = 0;
cpu_t g_num_online_cpus = 0;

struct cpu_mask g_online_cpus;
