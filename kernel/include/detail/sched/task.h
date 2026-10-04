#pragma once

#include <common/types.h>

#include <sched/task.h>

#define TASK_NAME_MAX 32

struct task {
    char name[TASK_NAME_MAX];
};
