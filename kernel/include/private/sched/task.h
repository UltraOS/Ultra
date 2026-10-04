#pragma once

#include <common/atomic.h>

#include <per_cpu.h>
#include <detail/sched/task.h>

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
