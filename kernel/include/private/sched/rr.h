#pragma once

#include <common/types.h>
#include <common/list.h>

#include <sched/task.h>
#include <private/sched/task.h>
#include <private/sched/classes.h>

struct sched_class;

struct rr_rq {
    struct list_link queues[TASK_NUM_PRIOS];
    u32 non_empty_queues_mask;
};

DECLARE_SCHED_CLASS(rr);

void rr_rq_init(struct rr_rq *rr);
