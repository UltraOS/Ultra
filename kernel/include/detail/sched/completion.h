#pragma once

#include <common/types.h>

#include <detail/sched/wait_queue.h>

struct completion {
    u32 done;
    struct wait_queue wq;
};
