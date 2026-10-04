#pragma once

#include <common/attributes.h>

#include <sched/task.h>

/*
 * Prepare the stack of a task that has never run, so that the first
 * arch_switch_to() into it calls task_entry() with 'fn' and 'user'.
 */
void arch_task_frame_init(struct task *task, task_fn_t fn, void *user);

/*
 * Perform a context switch from 'prev' to 'next'. Once the CPU is
 * yielded back to 'prev', the return value contains the task that
 * performed the switch.
 */
struct task *arch_switch_to(struct task *prev, struct task *next);

/*
 * The first generic code a new task runs, called from the frame set up by
 * arch_task_frame_init(). The architecture must pass the return value of
 * the arch_switch_to() call that first spun up the task as 'prev'.
 */
NORETURN
void task_entry(struct task *prev, task_fn_t fn, void *user);
