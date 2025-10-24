#pragma once

#include <common/types.h>

struct cpuid_res {
    u32 a;
    u32 b;
    u32 c;
    u32 d;
};
void cpuid(u32 function, struct cpuid_res *id);
void cpuid_subleaf(u32 function, u32 subleaf, struct cpuid_res *id);
