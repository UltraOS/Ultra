#pragma once

#define ARCH_HAS_CUSTOM_PREEMPT_COUNT

#include <common/types.h>
#include <common/attributes.h>
#include <common/bit.h>

#include <per_cpu.h>

/*
 * Stored inverted, so that a count of zero means both that preemption
 * is enabled and that a reschedule is wanted, which lets the enable
 * path decide with one decrement.
 */
#define X86_PREEMPT_RESCHED_NOT_NEEDED BIT_U32(31)

DECLARE_PER_CPU(u32, g_this_cpu_preempt_count);

static ALWAYS_INLINE u32 preempt_count(void)
{
    return this_cpu_read(g_this_cpu_preempt_count) &
           ~X86_PREEMPT_RESCHED_NOT_NEEDED;
}

static ALWAYS_INLINE void preempt_count_add(u32 value)
{
    this_cpu_add(g_this_cpu_preempt_count, value);
}

static ALWAYS_INLINE void preempt_count_sub(u32 value)
{
    this_cpu_sub(g_this_cpu_preempt_count, value);
}
