#pragma once

// Lets a test make a spin loop progress from the outside
static void (*g_cpu_relax_hook)(void);

static inline void arch_cpu_relax(void)
{
    if (g_cpu_relax_hook)
        g_cpu_relax_hook();
}
