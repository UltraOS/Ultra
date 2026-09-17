#pragma once

#include <common/attributes.h>

static inline void arch_cpu_relax(void)
{
    asm volatile("pause" ::: "memory");
}

static NORETURN inline void arch_cpu_halt(void)
{
    for (;;)
        asm volatile("cli; hlt" ::: "memory");
}
