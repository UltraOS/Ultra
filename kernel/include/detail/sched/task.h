#pragma once

#include <common/types.h>
#include <common/helpers.h>
#include <common/bit.h>
#include <common/atomic.h>

#include <config.h>
#include <locking/spinlock.h>
#include <sched/task.h>

#include <detail/refcount.h>
#include <detail/sched/kthread.h>
#include <detail/sched/rr.h>
#include <arch/detail/task.h>

// We need to know whether this arch keeps preempt_count in struct task
#if HAS_INCLUDE(<arch/detail/preempt.h>)
#include <arch/detail/preempt.h>
#endif

struct sched_class;

struct ktest_run;

#define TASK_NAME_MAX 32

enum task_flags : u32 {
    TASK_FLAG_NONE = 0,
    TASK_FLAG_NEED_RESCHED = BIT_U32(0),
    TASK_FLAG_KTHREAD = BIT_U32(1),
    TASK_FLAG_KTHREAD_SHOULD_STOP = BIT_U32(2),
#if IS_ENABLED(UBSAN)
    TASK_FLAG_IN_UBSAN_REPORT = BIT_U32(31),
#endif
};

enum task_state : u32;

struct task {
    enum task_flags flags;
#ifndef ARCH_HAS_CUSTOM_PREEMPT_COUNT
    u32 preempt_count;
#endif
    enum task_state state;
    struct refcount32 refcount;
    struct spinlock lock;

    cpu_t cpu;
    u8 prio;
    bool on_rq;
    bool on_cpu;
    const struct sched_class *sched_class;

    union {
        struct rr_entity rr;
    };

    char name[TASK_NAME_MAX];
    void *stack;
    size_t stack_size;

    union {
        struct kthread kthread;
    };

#if IS_ENABLED(KTEST)
    struct ktest_run *ktest_run;
#endif

    struct arch_task arch;
};

static inline bool task_test_flag(struct task *task, enum task_flags flag)
{
    return atomic_load_relaxed(&task->flags) & flag;
}
