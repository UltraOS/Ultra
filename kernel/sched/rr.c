#include <private/sched/core.h>
#include <private/sched/rr.h>

#include <common/bit.h>

void rr_rq_init(struct rr_rq *rr)
{
    size_t i;

    for (i = 0; i < TASK_NUM_PRIOS; i++)
        list_init(&rr->queues[i]);

    rr->non_empty_queues_mask = 0;
}

static void rr_enqueue(struct rq *rq, struct task *task)
{
    struct rr_rq *rr = &rq->rr;

    list_insert_prev(&rr->queues[task->prio], &task->rr.link);
    rr->non_empty_queues_mask |= BIT_U32(task->prio);
}

static void rr_dequeue(struct rq *rq, struct task *task)
{
    struct rr_rq *rr = &rq->rr;

    list_remove(&task->rr.link);

    if (list_is_empty(&rr->queues[task->prio]))
        rr->non_empty_queues_mask &= ~BIT_U32(task->prio);
}

static struct task *rr_pick(struct rq *rq)
{
    struct rr_rq *rr = &rq->rr;
    u32 prio;

    if (rr->non_empty_queues_mask == 0)
        return nullptr;

    prio = BIT_FIND_FIRST_SET(rr->non_empty_queues_mask) - 1;
    return list_first_entry(&rr->queues[prio], struct task, rr.link);
}

DEFINE_SCHED_CLASS(rr) = {
    .enqueue = rr_enqueue,
    .dequeue = rr_dequeue,
    .pick = rr_pick,
};
