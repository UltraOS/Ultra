#include <private/sched/core.h>
#include <private/sched/rr.h>

#include <common/bit.h>
#include <common/overflow.h>

#include <time/time.h>
#include <time/units.h>

#define RR_SLICE_NS (10 * NS_PER_MS)

void rr_rq_init(struct rr_rq *rr)
{
    size_t i;

    for (i = 0; i < TASK_NUM_PRIOS; i++)
        list_init(&rr->queues[i]);

    rr->non_empty_queues_mask = 0;
}

/*
 * A task only competes with others of its own priority, and without
 * any there is nothing to rotate to, so no deadline is armed for it.
 */
static bool rr_is_alone(struct rq *rq, struct task *task)
{
    return list_is_singular(&rq->rr.queues[task->prio]);
}

static void rr_charge(struct task *task, u64 now_ns)
{
    u64 ran_ns;

    ran_ns = now_ns - task->rr.running_since_ns;
    task->rr.running_since_ns = now_ns;

    task->rr.slice_left_ns = saturating_sub(task->rr.slice_left_ns, ran_ns);
}

static void rr_move_to_tail(struct rq *rq, struct task *task)
{
    struct rr_rq *rr = &rq->rr;

    list_remove(&task->rr.link);
    list_insert_prev(&rr->queues[task->prio], &task->rr.link);

    task->rr.slice_left_ns = RR_SLICE_NS;
}

/*
 * Called before 'peer' is queued. If 'peer' is the first task to share the
 * priority of rq->curr, arm the slice of rq->curr, or call resched_curr() if
 * that slice is used up already.
 */
static void rr_maybe_arm_slice(struct rq *rq, struct task *peer)
{
    struct task *curr = rq->curr;
    u64 now_ns;

    if (curr->sched_class != &SCHED_CLASS_INSTANCE(rr))
        return;
    if (curr->prio != peer->prio)
        return;

    // It already has a peer, so its slice is armed already
    if (!rr_is_alone(rq, curr))
        return;

    now_ns = ns_since_boot();
    rr_charge(curr, now_ns);

    /*
     * The running task now has a peer. Whatever it has run so far counts,
     * so that it's rotated out right away if it has been running alone for a
     * long time.
     */
    if (curr->rr.slice_left_ns == 0) {
        resched_curr(rq);
        return;
    }

    rq_arm_slice(rq, now_ns + curr->rr.slice_left_ns);
}

static void rr_enqueue(struct rq *rq, struct task *task)
{
    struct rr_rq *rr = &rq->rr;

    rr_maybe_arm_slice(rq, task);

    list_insert_prev(&rr->queues[task->prio], &task->rr.link);
    rr->non_empty_queues_mask |= BIT_U32(task->prio);

    task->rr.slice_left_ns = RR_SLICE_NS;
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

static void rr_yield(struct rq *rq)
{
    /*
     * The task gives up the rest of its slice, so the rr_switch_out() that
     * follows moves it to the tail of its queue and gives it a new slice.
     */
    rq->curr->rr.slice_left_ns = 0;
}

static void rr_wakeup_preempt(struct rq *rq, struct task *task)
{
    if (task->prio < rq->curr->prio)
        resched_curr(rq);
}

static void rr_switch_in(struct rq *rq)
{
    struct task *curr = rq->curr;
    u64 now_ns;

    now_ns = ns_since_boot();
    curr->rr.running_since_ns = now_ns;

    if (!rr_is_alone(rq, curr))
        rq_arm_slice(rq, now_ns + curr->rr.slice_left_ns);
}

static void rr_switch_out(struct rq *rq)
{
    struct task *curr = rq->curr;

    rq_cancel_slice(rq);
    rr_charge(curr, ns_since_boot());

    /*
     * A task that blocked is no longer queued, and gets a new slice once it
     * wakes up. A task that is still queued moves to the tail of its queue
     * with a new slice if it used up its slice. Otherwise, e.g. when it was
     * preempted by a higher priority, it keeps whatever remains of its slice
     * and stays at the head of the queue.
     */
    if (curr->on_rq && curr->rr.slice_left_ns == 0)
        rr_move_to_tail(rq, curr);
}

static void rr_slice_expired(struct rq *rq)
{
    struct task *curr = rq->curr;

    rr_charge(curr, ns_since_boot());

    if (rr_is_alone(rq, curr)) {
        curr->rr.slice_left_ns = RR_SLICE_NS;
        return;
    }

    resched_curr(rq);
}

DEFINE_SCHED_CLASS(rr) = {
    .enqueue = rr_enqueue,
    .dequeue = rr_dequeue,
    .pick = rr_pick,
    .yield = rr_yield,
    .wakeup_preempt = rr_wakeup_preempt,
    .switch_in = rr_switch_in,
    .switch_out = rr_switch_out,
    .slice_expired = rr_slice_expired,
};
