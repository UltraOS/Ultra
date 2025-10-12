#pragma once

static inline void arch_cpu_relax(void)
{
    asm volatile("pause" ::: "memory");
}
