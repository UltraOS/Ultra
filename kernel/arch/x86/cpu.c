#include <arch/private/cpu.h>

void cpuid(u32 function, struct cpuid_res *id)
void cpuid_subleaf(u32 function, u32 subleaf, struct cpuid_res *id)
{
    asm volatile("cpuid"
            : "=a"(id->a), "=b"(id->b), "=c"(id->c), "=d"(id->d)
            : "a"(function), "c"(subleaf));
}

void cpuid(u32 function, struct cpuid_res *id)
{
    cpuid_subleaf(function, 0, id);
}
