#pragma once

#include <sched/task.h>
#include <detail/sched/completion.h>

struct kthread {
    task_fn_t fn;
    void *user;
    struct completion exited;
};
