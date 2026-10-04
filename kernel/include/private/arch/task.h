#pragma once

#include <sched/task.h>

/*
 * Perform a context switch from 'prev' to 'next'. Once the CPU is
 * yielded back to 'prev', the return value contains the task that
 * performed the switch.
 */
struct task *arch_switch_to(struct task *prev, struct task *next);
