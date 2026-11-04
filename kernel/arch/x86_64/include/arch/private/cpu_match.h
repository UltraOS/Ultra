#pragma once

#include <common/types.h>

/*
 * An entry of a table of CPU models for cpu_match(), defined with
 * CPU_MATCH_VFM() or CPU_MATCH_VFM_WITH_STEPPINGS(). The table must be
 * terminated with an empty entry, e.g. "{}".
 */
struct cpu_match_entry {
    u32 vfm;
    u8 first_stepping;
    u8 last_stepping;

    /*
     * Private data not used by the matcher, e.g. the microcode revision that
     * fixes an erratum or a pointer to model-specific properties for a
     * workaround. The 'priv' parameter in CPU_MATCH_* macros accepts both
     * integers and pointers.
     */
    union {
        reg_t priv_data;
        const void *priv_ptr;
    };
};

#define CPU_STEPPING_MAX 0xF

// Match steppings 'first' through 'last' of 'cpu_vfm'
#define CPU_MATCH_VFM_WITH_STEPPINGS(cpu_vfm, first, last, priv) { \
    .vfm = (cpu_vfm),                                              \
    .first_stepping = (first),                                     \
    .last_stepping = (last),                                       \
    .priv_data = (reg_t)(priv),                                    \
}

// Match every stepping of 'cpu_vfm'
#define CPU_MATCH_VFM(cpu_vfm, priv)                                \
    CPU_MATCH_VFM_WITH_STEPPINGS(cpu_vfm, 0, CPU_STEPPING_MAX, priv)

/*
 * Returns the first entry in 'table' that matches the vendor, family, model and
 * stepping of the boot CPU, or nullptr if no matches were found.
 */
const struct cpu_match_entry *cpu_match(const struct cpu_match_entry *table);
