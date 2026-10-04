#pragma once

#include <common/attributes.h>

#include <bug.h>
#include <preempt.h>

struct registers;
void preempt_schedule_irq(struct registers *regs);

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

/*
 * The hook arch-specific interrupt handling code calls at the end of every
 * IRQ (NMI and exceptions never call this), after the EOI and with interrupts
 * still disabled.
 *
 * This leaves hard IRQ context, then checks whether a preemption is pending
 * and if it's allowed to actually perform it (the interrupted context had IRQs
 * enabled & preemption was not explicitly disabled), and if so, performs a
 * context switch.
 */
static ALWAYS_INLINE void hard_irq_exit(struct registers *regs)
{
    preempt_count_sub(PREEMPT_HARD_IRQ_UNIT);
    preempt_schedule_irq(regs);
}
