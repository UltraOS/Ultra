#include <private/sched/core.h>
#include <private/sched/idle.h>

#include <common/helpers.h>

#include <bug.h>
#include <irq_helpers.h>

#include <arch/cpu_helpers.h>

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

/*
 * The function every CPU's idle task runs. The check and the wait
 * are done with interrupts disabled so that a wakeup arriving in
 * between ends the wait instead of being slept through.
 */
void sched_idle_loop(void)
{
    for (;;) {
        local_irq_disable();

        if (need_resched()) {
            local_irq_enable();
            schedule_idle();
            continue;
        }

        arch_cpu_idle_wait();
    }
}
