#pragma once

// Arch-specific way to fetch 'current'
#include <arch/detail/current.h>

// A thread of execution, 'current' is the one running on this CPU
struct task;

void task_ref(struct task *task);
void task_unref(struct task *task);

const char *task_name(const struct task *task);
