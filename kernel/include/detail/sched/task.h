#pragma once

#include <common/types.h>
#include <common/helpers.h>
#include <common/bit.h>

#include <config.h>
#include <sched/task.h>

// We need to know whether this arch keeps preempt_count in struct task
#if HAS_INCLUDE(<arch/detail/preempt.h>)
#include <arch/detail/preempt.h>
#endif

#define TASK_NAME_MAX 32

enum task_flags : u32 {
    TASK_FLAG_NONE = 0,
#if IS_ENABLED(UBSAN)
    TASK_FLAG_IN_UBSAN_REPORT = BIT_U32(31),
#endif
};

struct task {
    enum task_flags flags;
#ifndef ARCH_HAS_CUSTOM_PREEMPT_COUNT
    u32 preempt_count;
#endif

    char name[TASK_NAME_MAX];
};
