#pragma once

#include <common/types.h>
#include <common/attributes.h>

#include <per_cpu.h>
#include <linker.h>
#include <locking/spinlock.h>

#include <private/sched/rr.h>
#include <private/sched/classes.h>
#include <private/sched/task.h>

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

    struct rr_rq rr;
};

DECLARE_PER_CPU(struct rq, g_this_cpu_rq);

#define this_rq() this_cpu_ptr(&g_this_cpu_rq)
#define cpu_rq(cpu) per_cpu_ptr(&g_this_cpu_rq, cpu)
#define task_rq(task) cpu_rq((task)->cpu)

void rq_enqueue(struct rq *rq, struct task *task);
void rq_dequeue(struct rq *rq, struct task *task);
struct task *rq_pick_next(struct rq *rq);
