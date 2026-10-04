#pragma once

#include <common/attributes.h>
#include <common/atomic.h>

#include <detail/preempt.h>

static ALWAYS_INLINE void preempt_disable(void)
{
    preempt_count_add(PREEMPT_DISABLE_UNIT);
    compiler_barrier();
}

static ALWAYS_INLINE void preempt_enable(void)
{
    compiler_barrier();

    if (unlikely(preempt_count_sub_and_test(PREEMPT_DISABLE_UNIT)))
        preempt_schedule();
}

// True while executing in hardware interrupt context on this CPU
static ALWAYS_INLINE bool in_hard_irq(void)
{
    return preempt_count() & PREEMPT_HARD_IRQ_MASK;
}
