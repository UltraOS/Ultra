#pragma once

#include <common/helpers.h>

#include <config.h>

struct task;

#if IS_ENABLED(KTEST)
/*
 * Called for every new kernel thread, which then reports to the same run as
 * the thread that created it and holds a reference to that run.
 */
void ktest_inherit_run(struct task *task);

// Called when a kernel thread is freed, drops the reference to its run
void ktest_release_run(struct task *task);
#else
static inline void ktest_inherit_run(struct task *task)
{
    UNREFERENCED_PARAMETER(task);
}

static inline void ktest_release_run(struct task *task)
{
    UNREFERENCED_PARAMETER(task);
}
#endif
