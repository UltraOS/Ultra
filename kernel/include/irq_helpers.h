#pragma once

#include <common/types.h>
#include <common/attributes.h>

#include <arch/private/irq_helpers.h>

static ALWAYS_INLINE bool irq_enabled(void)
{
    return arch_irq_is_enabled_state(arch_irq_state());
}

// True while executing in hardware interrupt context on this CPU
bool in_hard_irq(void);

/*
 * Save the arch-specific IRQ state and disable IRQs if they were previously
 * enabled.
 */
static ALWAYS_INLINE irq_state_t irq_state_save(void)
{
    irq_state_t state;

    state = arch_irq_state();
    if (arch_irq_is_enabled_state(state))
        arch_irq_disable();

    return state;
}

// Restore the IRQ state from the arch-specific state saved earlier
static ALWAYS_INLINE void irq_state_restore(irq_state_t state)
{
    if (!arch_irq_is_enabled_state(state))
        return;

    arch_irq_enable();
}
