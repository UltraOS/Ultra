#pragma once

#include <per_cpu.h>

struct task;

DECLARE_PER_CPU(struct task*, g_this_cpu_task);

#define current this_cpu_read(g_this_cpu_task)
