#pragma once

#include <common/atomic.h>

#include <memory/units.h>
#include <per_cpu.h>
#include <detail/sched/task.h>

// Lower priority value runs first
#define TASK_PRIO_HIGHEST 0
#define TASK_PRIO_DEFAULT 16
#define TASK_PRIO_LOWEST 31
#define TASK_NUM_PRIOS (TASK_PRIO_LOWEST + 1)

#define TASK_STACK_SIZE (16 * KiB)

DECLARE_PER_CPU(struct task, g_this_cpu_idle_task);

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
