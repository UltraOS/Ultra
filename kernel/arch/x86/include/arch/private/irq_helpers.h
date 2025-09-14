#pragma once

#include <common/types.h>
#include <common/attributes.h>

#define X86_RFLAGS_IF (1 << 9)

static ALWAYS_INLINE void arch_irq_enable(void)
{
    asm volatile("sti" ::: "memory");
}

static ALWAYS_INLINE void arch_irq_disable(void)
{
    asm volatile("cli" ::: "memory");
}

static ALWAYS_INLINE bool arch_irq_is_enabled_state(irq_state_t flags)
{
    return flags & X86_RFLAGS_IF;
}

static ALWAYS_INLINE irq_state_t arch_irq_state(void)
{
    irq_state_t state;

    asm volatile("pushf; pop %0" : "=rm" (state) :: "memory");
    return state;
}
