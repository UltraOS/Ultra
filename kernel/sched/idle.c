#include <private/sched/core.h>
#include <private/sched/idle.h>

#include <common/helpers.h>

#include <bug.h>

static void idle_enqueue(struct rq *rq, struct task *task)
{
    UNREFERENCED_PARAMETER(rq);
    UNREFERENCED_PARAMETER(task);

    BUG();
}

static void idle_dequeue(struct rq *rq, struct task *task)
{
    UNREFERENCED_PARAMETER(rq);
    UNREFERENCED_PARAMETER(task);

    BUG();
}

static struct task *idle_pick(struct rq *rq)
{
    return rq->idle;
}

static void idle_yield(struct rq *rq)
{
    UNREFERENCED_PARAMETER(rq);

    BUG();
}

static void idle_wakeup_preempt(struct rq *rq, struct task *task)
{
    UNREFERENCED_PARAMETER(rq);
    UNREFERENCED_PARAMETER(task);

    BUG();
}

DEFINE_SCHED_CLASS(idle) = {
    .enqueue = idle_enqueue,
    .dequeue = idle_dequeue,
    .pick = idle_pick,
    .yield = idle_yield,
    .wakeup_preempt = idle_wakeup_preempt,
};
