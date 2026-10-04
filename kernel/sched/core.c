#include <private/sched/core.h>
#include <private/sched/rr.h>
#include <private/sched/idle.h>

#include <common/helpers.h>

#include <bug.h>
#include <linker.h>
#include <smp.h>
#include <init_level.h>
#include <free_after_init.h>

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

    for_each_section_array_item(class, SCHED_CLASSES_SECTION) {
        task = class->pick(rq);
        if (task != nullptr)
            return task;
    }

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
