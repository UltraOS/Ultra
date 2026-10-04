#pragma once

#include <common/types.h>
#include <common/atomic.h>
#include <common/attributes.h>
#include <common/helpers.h>
#include <common/bit.h>

#define PREEMPT_DISABLE_UNIT BIT_U32(0)
#define PREEMPT_DISABLE_MASK MAKE_BIT_MASK_U32(7, 0)
#define PREEMPT_DISABLE_DEPTH(x) ((x) * PREEMPT_DISABLE_UNIT)

#define PREEMPT_HARD_IRQ_UNIT BIT_U32(8)
#define PREEMPT_HARD_IRQ_MASK MAKE_BIT_MASK_U32(11, 8)
#define PREEMPT_HARD_IRQ_DEPTH(x) ((x) * PREEMPT_HARD_IRQ_UNIT)

#if HAS_INCLUDE(<arch/detail/preempt.h>)
#include <arch/detail/preempt.h>
#endif

#ifndef ARCH_HAS_CUSTOM_PREEMPT_COUNT
#include <sched/task.h>
#include <detail/sched/task.h>

static ALWAYS_INLINE u32 preempt_count(void)
{
    return atomic_load_relaxed(&current->preempt_count);
}

static ALWAYS_INLINE void preempt_count_add(u32 value)
{
    u32 count;

    count = atomic_load_relaxed(&current->preempt_count);
    atomic_store_relaxed(&current->preempt_count, count + value);
}

static ALWAYS_INLINE void preempt_count_sub(u32 value)
{
    u32 count;

    count = atomic_load_relaxed(&current->preempt_count);
    atomic_store_relaxed(&current->preempt_count, count - value);
}

static ALWAYS_INLINE bool preempt_should_resched(void)
{
    return atomic_load_relaxed(&current->preempt_count) == 0 &&
           task_test_flag(current, TASK_FLAG_NEED_RESCHED);
}

static ALWAYS_INLINE bool preempt_count_sub_and_test(u32 value)
{
    struct task *task = current;
    u32 count;

    count = atomic_load_relaxed(&task->preempt_count) - value;
    atomic_store_relaxed(&task->preempt_count, count);
    if (count != 0)
        return false;

    /*
     * The count must be stored before the flag is read since an IRQ after the
     * store sees zero and preempts on its own way out, and an IRQ before sets
     * the flag that is read here.
     */
    compiler_barrier();
    return task_test_flag(task, TASK_FLAG_NEED_RESCHED);
}

static ALWAYS_INLINE void preempt_set_need_resched(void)
{
}

static ALWAYS_INLINE void preempt_clear_need_resched(void)
{
}
#endif

void preempt_schedule(void);
