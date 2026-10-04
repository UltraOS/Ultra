#pragma once

#include <common/attributes.h>

static inline void arch_cpu_relax(void)
{
    asm volatile("pause" ::: "memory");
}

/*
 * Called with interrupts disabled, returns with them enabled once one
 * has been taken. sti only takes effect after the next instruction, so
 * nothing can be delivered between the two and get slept through.
 */
static inline void arch_cpu_idle_wait(void)
{
    asm volatile("sti; hlt" ::: "memory");
}

static NORETURN inline void arch_cpu_halt(void)
{
    for (;;)
        asm volatile("cli; hlt" ::: "memory");
}
