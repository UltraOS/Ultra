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

// True if nothing keeps preemption off anymore and a reschedule was pending
static ALWAYS_INLINE bool preempt_count_sub_and_test(u32 value)
{
    bool is_zero;

    asm volatile(
        "subl %[value], %%gs:%[count]"
        : "=@ccz" (is_zero), [count] "+m" (g_this_cpu_preempt_count)
        : [value] "ri" (value)
    );

    return is_zero;
}

static ALWAYS_INLINE bool preempt_should_resched(void)
{
    return this_cpu_read(g_this_cpu_preempt_count) == 0;
}

static ALWAYS_INLINE void preempt_set_need_resched(void)
{
    this_cpu_and(g_this_cpu_preempt_count, ~X86_PREEMPT_RESCHED_NOT_NEEDED);
}

static ALWAYS_INLINE void preempt_clear_need_resched(void)
{
    this_cpu_or(g_this_cpu_preempt_count, X86_PREEMPT_RESCHED_NOT_NEEDED);
}
