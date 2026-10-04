#pragma once

#include <common/types.h>
#include <common/attributes.h>

#include <per_cpu.h>
#include <linker.h>
#include <preempt.h>
#include <locking/spinlock.h>
#include <time/timer.h>

#include <private/sched/sched.h>
#include <private/sched/rr.h>
#include <private/sched/classes.h>
#include <private/sched/task.h>

#define PREEMPT_COUNT_AT_SWITCH PREEMPT_DISABLE_DEPTH(2)

struct rq;

/*
 * A set of operations of one scheduling algorithm. Every task belongs to
 * exactly one of these classes. Each class keeps its own state in the runqueue
 * and is responsible for ordering all tasks that belong to it.
 *
 * The set of classes is fixed at build time and the scheduling core orders them
 * internally by rank. A class only gets to pick when every class above it has
 * nothing runnable
 *
 * Every operation is called with the runqueue locked and interrupts disabled.
 */
struct sched_class {
    /*
     * Add a task that became runnable, or remove one that stopped being
     * runnable. A task stays queued while it runs.
     */
    void (*enqueue)(struct rq *rq, struct task *task);
    void (*dequeue)(struct rq *rq, struct task *task);

    /*
     * Return the task this class wants to run next, leaving it queued, or
     * nullptr if the class has nothing runnable.
     */
    struct task *(*pick)(struct rq *rq);

    // Called when rq->curr yields the CPU to its peers but stays runnable
    void (*yield)(struct rq *rq);

    /*
     * Called when 'task' wakes up on 'rq' and is of the same class as
     * rq->curr. The callback is expected to decide whether 'task' is more
     * important than rq->curr and call resched_curr() if that's the case.
     *
     * NOTE: For a 'task' of a class ranked above that of rq->curr,
     *       resched_curr() is invoked automatically.
     */
    void (*wakeup_preempt)(struct rq *rq, struct task *task);

    /*
     * Called on every pass through schedule(), switch_out() for rq->curr
     * before the next task is picked, then switch_in() once the task picked
     * is rq->curr. A task runs from its switch_in() call until its
     * switch_out() call.
     *
     * NOTE: The task picked may be rq->curr again, in which case it gets
     *       switch_out() and switch_in() back to back without a context
     *       switch. rq->curr may already be dequeued when switch_out() is
     *       called.
     */
    void (*switch_in)(struct rq *rq);
    void (*switch_out)(struct rq *rq);

    // Called for rq->curr once the deadline of rq_arm_slice() expires
    void (*slice_expired)(struct rq *rq);
};

#define DEFINE_SCHED_CLASS(name)                                      \
    SECTION_VAR(SCHED_CLASS_SECTION(name), const, struct sched_class) \
    SCHED_CLASS_INSTANCE(name)

DECLARE_TYPED_SECTION_MARKERS(const struct sched_class, SCHED_CLASSES_SECTION);

struct rq {
    struct spinlock lock;

    struct task *curr;
    struct task *idle;

    /*
     * Number of runnable tasks on this runqueue, including the currently
     * running task. The idle task is not accounted here.
     */
    u32 nr_running;
    u64 nr_switches;

    struct timer slice_timer;

    struct rr_rq rr;
};

DECLARE_PER_CPU(struct rq, g_this_cpu_rq);

#define this_rq() this_cpu_ptr(&g_this_cpu_rq)
#define cpu_rq(cpu) per_cpu_ptr(&g_this_cpu_rq, cpu)
#define task_rq(task) cpu_rq((task)->cpu)

static inline bool need_resched(void)
{
    return task_test_flag(current, TASK_FLAG_NEED_RESCHED);
}

void rq_enqueue(struct rq *rq, struct task *task);
void rq_dequeue(struct rq *rq, struct task *task);
struct task *rq_pick_next(struct rq *rq);
void resched_curr(struct rq *rq);

void rq_arm_slice(struct rq *rq, u64 deadline_ns);
void rq_cancel_slice(struct rq *rq);

void schedule_idle(void);
void sched_finish_switch(struct task *prev);
