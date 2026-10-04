#pragma once

#include <common/attributes.h>
#include <common/atomic.h>
#include <common/bit.h>

#include <memory/units.h>
#include <per_cpu.h>
#include <detail/sched/task.h>

// Lower priority value runs first
#define TASK_PRIO_HIGHEST 0
#define TASK_PRIO_DEFAULT 16
#define TASK_PRIO_LOWEST 31
#define TASK_NUM_PRIOS (TASK_PRIO_LOWEST + 1)

#define TASK_STACK_SIZE (16 * KiB)

enum task_state : u32 {
    TASK_STATE_RUNNING = 0,
    TASK_STATE_UNINTERRUPTIBLE = BIT_U32(0),

    /*
     * A task is TASK_STATE_NEW until it first runs and TASK_STATE_DEAD once
     * it exits
     */
    TASK_STATE_NEW = BIT_U32(8),
    TASK_STATE_DEAD = BIT_U32(9),
};

DECLARE_PER_CPU(struct task, g_this_cpu_idle_task);

static inline enum task_state task_state(struct task *task)
{
    return atomic_load_relaxed(&task->state);
}

static inline void task_set_state(struct task *task, enum task_state state)
{
    atomic_store_relaxed(&task->state, state);
}

static inline bool task_test_and_set_flag(
    struct task *task, enum task_flags flag
)
{
    return atomic_fetch_or(&task->flags, flag, MO_ACQ_REL) & flag;
}

static inline void task_clear_flag(struct task *task, enum task_flags flag)
{
    atomic_fetch_and(&task->flags, ~flag, MO_RELEASE);
}

struct task *task_alloc(void);

NORETURN
void task_exit(void);
