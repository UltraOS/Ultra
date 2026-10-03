#pragma once

#include <common/attributes.h>

#include <bug.h>
#include <preempt.h>

/*
 * The hook arch-specific interrupt handling code calls upon entry into an
 * IRQ handler (NMI and exceptions never call this).
 *
 * This does basic hard irq bookkeeping & makes sure IRQ contexts never nest.
 */
static ALWAYS_INLINE void hard_irq_enter(void)
{
    BUG_ON(in_hard_irq());
    preempt_count_add(PREEMPT_HARD_IRQ_UNIT);
}

static ALWAYS_INLINE void hard_irq_exit(void)
{
    preempt_count_sub(PREEMPT_HARD_IRQ_UNIT);
}
