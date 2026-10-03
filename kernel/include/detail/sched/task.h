#pragma once

#include <common/types.h>
#include <common/helpers.h>

#include <sched/task.h>

// We need to know whether this arch keeps preempt_count in struct task
#if HAS_INCLUDE(<arch/detail/preempt.h>)
#include <arch/detail/preempt.h>
#endif

#define TASK_NAME_MAX 32

struct task {
#ifndef ARCH_HAS_CUSTOM_PREEMPT_COUNT
    u32 preempt_count;
#endif

    char name[TASK_NAME_MAX];
};
