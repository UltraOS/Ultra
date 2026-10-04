#pragma once

#include <common/atomic.h>
#include <common/types.h>

#include <private/sched/task.h>
#include <detail/sched/sched.h>

/*
 * The store is followed by a full barrier, so that a waiter can set its
 * state, check its condition and then schedule() without missing a wakeup
 * that makes the condition true in between.
 */
#define set_current_state(new_state) ({ \
    task_set_state(current, new_state); \
    barrier_full();                     \
})

/*
 * Make 'task' runnable if it is in one of the states in 'state_mask'.
 * Returns whether it was. Callable from any context that may take a
 * spinlock, the caller must hold a reference to the task.
 */
bool wake_up_task(struct task *task, enum task_state state_mask);

/*
 * Stay runnable but let the other tasks of the same priority run first.
 *
 * NOTE: This is not a way to wait for another task, and tasks of a lower
 *       priority never get to run.
 */
void yield(void);
