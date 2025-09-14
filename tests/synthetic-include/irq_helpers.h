#pragma once

static __thread bool g_enabled = true;

static inline bool irq_enabled(void)
{
    return g_enabled;
}

static inline irq_state_t irq_state_save(void)
{
    bool was_enabled = g_enabled;

    g_enabled = false;
    return was_enabled;
}

static inline void irq_state_restore(irq_state_t state)
{
    g_enabled = state;
}
