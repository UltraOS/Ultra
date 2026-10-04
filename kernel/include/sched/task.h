#pragma once

// Arch-specific way to fetch 'current'
#include <arch/detail/current.h>

// A thread of execution, 'current' is the one running on this CPU
struct task;

const char *task_name(const struct task *task);
