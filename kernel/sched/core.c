#include <private/sched/core.h>
#include <private/sched/rr.h>
#include <private/sched/idle.h>
#include <private/sched/task.h>
#include <private/arch/task.h>

#include <common/helpers.h>
#include <common/atomic.h>

#include <bug.h>
#include <linker.h>
#include <preempt.h>
#include <irq_helpers.h>
#include <smp.h>
#include <init_level.h>
#include <free_after_init.h>

#include <arch/cpu_helpers.h>

DEFINE_PER_CPU(struct rq, g_this_cpu_rq);

void rq_enqueue(struct rq *rq, struct task *task)
{
    BUG_ON(!spin_is_locked(&rq->lock));
    BUG_ON(task->on_rq);

    task->sched_class->enqueue(rq, task);
    task->on_rq = true;
    rq->nr_running++;
}

void rq_dequeue(struct rq *rq, struct task *task)
{
    BUG_ON(!spin_is_locked(&rq->lock));
    BUG_ON(!task->on_rq);

    task->sched_class->dequeue(rq, task);
    task->on_rq = false;
    rq->nr_running--;
}

struct task *rq_pick_next(struct rq *rq)
{
    const struct sched_class *class;
    struct task *task;

    BUG_ON(!spin_is_locked(&rq->lock));

    for (class = SECTION_ARRAY_BEGIN(SCHED_CLASSES_SECTION);
         class < SECTION_ARRAY_END(SCHED_CLASSES_SECTION); class++) {
        task = class->pick(rq);
        if (task != nullptr)
            return task;
    }

    BUG();
}

/*
 * The second half of a switch, run by the task that was switched to. The
 * runqueue lock was taken by 'prev' and is released here, and every task
 * gets here with the preempt count at two: one from whoever called
 * do_schedule() and one for that lock.
 */
void sched_finish_switch(struct task *prev)
{
    struct rq *rq;
    enum task_state prev_state;

    rq = this_rq();
    BUG_ON(preempt_count() != PREEMPT_COUNT_AT_SWITCH);

    prev_state = task_state(prev);
    atomic_store_release(&prev->on_cpu, false);

    spin_unlock(&rq->lock);
    local_irq_enable();

    /*
     * A task cannot free its own stack that it's running on, so free it here
     * now that we're off it.
     */
    if (prev_state == TASK_STATE_DEAD)
        task_unref(prev);
}

/*
 * Called with preemption disabled exactly once and interrupts enabled.
 * A task that is not runnable anymore is taken off the runqueue here,
 * and only here, so that a wakeup arriving before this point finds it
 * still queued.
 */
static void do_schedule(void)
{
    struct rq *rq;
    struct task *prev, *next;
    enum task_state prev_state;

    BUG_ON(in_hard_irq());
    BUG_ON(preempt_count() != PREEMPT_DISABLE_DEPTH(1));
    BUG_ON(!local_irq_enabled());

    local_irq_disable();

    rq = this_rq();
    spin_lock(&rq->lock);

    prev = rq->curr;
    prev_state = task_state(prev);

    if (prev_state != TASK_STATE_RUNNING) {
        BUG_ON(prev == rq->idle);
        rq_dequeue(rq, prev);
    }

    next = rq_pick_next(rq);
    if (next == prev) {
        spin_unlock(&rq->lock);
        local_irq_enable();
        return;
    }

    rq->nr_switches++;
    rq->curr = next;
    atomic_store_relaxed(&next->on_cpu, true);

    prev = arch_switch_to(prev, next);
    sched_finish_switch(prev);
}

bool wake_up_task(struct task *task, enum task_state state_mask)
{
    struct rq *rq;
    irq_state_t irq_state;
    bool woken = false;

    irq_state = spin_lock_irq_save(&task->lock);
    /*
     * Pairs with the barrier in set_current_state(), the sleeper stores
     * its state and then checks its condition, the waker stores the
     * condition and then loads the state here, so one of the two is
     * guaranteed to see the other's store
     */
    barrier_full();

    if (!(task_state(task) & state_mask))
        goto out;

    rq = task_rq(task);
    spin_lock(&rq->lock);

    /*
     * Still queued, so its own schedule() call, whenever it gets there,
     * will find it runnable and keep it queued.
     */
    if (task->on_rq) {
        task_set_state(task, TASK_STATE_RUNNING);
        spin_unlock(&rq->lock);

        woken = true;
        goto out;
    }

    spin_unlock(&rq->lock);

    // Its CPU may still be in the middle of switching away from it
    while (atomic_load_acquire(&task->on_cpu))
        arch_cpu_relax();

    rq = task_rq(task);
    spin_lock(&rq->lock);

    /*
     * Give the task its own reference so it cannot be freed via an external
     * task_unref() while it's running. This pairs with task_unref() inside
     * sched_finish_switch() when we detect that this task has exited.
     */
    if (task_state(task) == TASK_STATE_NEW)
        task_ref(task);

    task_set_state(task, TASK_STATE_RUNNING);
    rq_enqueue(rq, task);

    spin_unlock(&rq->lock);
    woken = true;

out:
    spin_unlock_irq_restore(&task->lock, irq_state);
    return woken;
}

void schedule(void)
{
    preempt_disable();
    do_schedule();
    preempt_enable();
}

void schedule_idle(void)
{
    /*
     * The idle task runs at a preempt depth of one for its whole life,
     * so the level schedule() adds around this call is already there.
     */
    do_schedule();
}

void yield(void)
{
    struct rq *rq;
    irq_state_t irq_state;

    preempt_disable();

    rq = this_rq();

    irq_state = spin_lock_irq_save(&rq->lock);
    current->sched_class->yield(rq);
    spin_unlock_irq_restore(&rq->lock, irq_state);

    do_schedule();
    preempt_enable();
}

void task_exit(void)
{
    struct task *task = current;
    irq_state_t irq_state;

    preempt_disable();

    irq_state = spin_lock_irq_save(&task->lock);
    task_set_state(task, TASK_STATE_DEAD);
    spin_unlock_irq_restore(&task->lock, irq_state);

    do_schedule();
    BUG();
}

static void rq_init(struct rq *rq, cpu_t cpu)
{
    struct task *idle;

    idle = per_cpu_ptr(&g_this_cpu_idle_task, cpu);
    idle->cpu = cpu;
    idle->sched_class = &SCHED_CLASS_INSTANCE(idle);

    spin_lock_init(&rq->lock);
    rr_rq_init(&rq->rr);

    idle->on_cpu = true;

    rq->idle = idle;
    rq->curr = idle;
}

static error_t INIT_CODE sched_init(void)
{
    cpu_t cpu;

    for_each_present_cpu(cpu)
        rq_init(cpu_rq(cpu), cpu);

    return EOK;
}
INIT_CALL_POST(PER_CPU_AVAILABLE, sched_init);
