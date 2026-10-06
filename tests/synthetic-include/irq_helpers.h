#pragma once

#include <common/types.h>

extern __thread bool g_test_irqs_enabled;

static inline bool local_irq_enabled(void)
{
    return g_test_irqs_enabled;
}

static inline irq_state_t local_irq_state_save(void)
{
    bool was_enabled = g_test_irqs_enabled;

    g_test_irqs_enabled = false;
    return was_enabled;
}

static inline void local_irq_state_restore(irq_state_t state)
{
    g_test_irqs_enabled = state;
}

static inline bool in_hard_irq(void)
{
    return false;
}
