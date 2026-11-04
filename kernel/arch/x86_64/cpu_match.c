#include <arch/private/cpu_match.h>
#include <arch/private/cpu.h>

const struct cpu_match_entry *cpu_match(const struct cpu_match_entry *table)
{
    const struct cpu_match_entry *id;
    u32 vfm;

    vfm = X86_VFM(g_cpu_info.vendor, g_cpu_info.family, g_cpu_info.model);

    // VFM of 0 means end-of-table
    for (id = table; id->vfm != 0; id++) {
        if (id->vfm != vfm)
            continue;
        if (g_cpu_info.stepping < id->first_stepping ||
            g_cpu_info.stepping > id->last_stepping)
            continue;

        return id;
    }

    return nullptr;
}
