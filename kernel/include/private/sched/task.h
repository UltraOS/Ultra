#pragma once

#include <per_cpu.h>
#include <detail/sched/task.h>

DECLARE_PER_CPU(struct task, g_this_cpu_idle_task);
