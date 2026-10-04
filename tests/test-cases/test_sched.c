#include <string.h>

#include <memory/alloc_behavior.h>

extern int g_sched_test_live_tasks, g_sched_test_live_stacks;

void *sched_test_alloc(size_t size, enum alloc_behavior behavior);
void sched_test_free(void *ptr);
void *sched_test_valloc(size_t size, enum alloc_behavior behavior);
void sched_test_vrelease(void *ptr);

#define alloc sched_test_alloc
#define free sched_test_free
#define valloc sched_test_valloc
#define vrelease sched_test_vrelease

#include <kernel-source/sched/task.c>
#include <kernel-source/sched/core.c>
#include <kernel-source/sched/rr.c>
#include <kernel-source/sched/idle.c>

#include <test_harness.h>

#define NUM_TEST_TASKS 8

static struct task s_tasks[NUM_TEST_TASKS];

static struct rq *reset_state(void)
{
    struct rq *rq;
    size_t i;

    g_num_present_cpus = 1;
    g_per_cpu_offset[0] = 0;

    memset(&g_this_cpu_rq, 0, sizeof(g_this_cpu_rq));
    sched_init();

    for (i = 0; i < NUM_TEST_TASKS; i++) {
        s_tasks[i] = (struct task) {
            .prio = TASK_PRIO_DEFAULT,
            .sched_class = &SCHED_CLASS_INSTANCE(rr),
        };
    }

    rq = cpu_rq(0);
    spin_lock(&rq->lock);

    return rq;
}

static void finish(struct rq *rq)
{
    spin_unlock(&rq->lock);
}

TEST_CASE(sched_init_installs_idle)
{
    struct rq *rq;

    rq = reset_state();

    ASSERT_EQ(rq->idle, &g_this_cpu_idle_task);
    ASSERT_EQ(rq->curr, &g_this_cpu_idle_task);
    ASSERT_EQ(rq->nr_running, 0);
    ASSERT_EQ(g_this_cpu_idle_task.cpu, 0);
    ASSERT_EQ(g_this_cpu_idle_task.sched_class, &SCHED_CLASS_INSTANCE(idle));

    finish(rq);
}

TEST_CASE(sched_pick_idle_when_empty)
{
    struct rq *rq;

    rq = reset_state();

    ASSERT_EQ(rq_pick_next(rq), rq->idle);

    rq_enqueue(rq, &s_tasks[0]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[0]);

    rq_dequeue(rq, &s_tasks[0]);
    ASSERT_EQ(rq_pick_next(rq), rq->idle);

    finish(rq);
}

TEST_CASE(sched_rr_fifo_within_priority)
{
    struct rq *rq;
    size_t i;

    rq = reset_state();

    for (i = 0; i < NUM_TEST_TASKS; i++)
        rq_enqueue(rq, &s_tasks[i]);
    ASSERT_EQ(rq->nr_running, NUM_TEST_TASKS);

    for (i = 0; i < NUM_TEST_TASKS; i++) {
        ASSERT_EQ(rq_pick_next(rq), &s_tasks[i]);
        ASSERT(s_tasks[i].on_rq);

        rq_dequeue(rq, &s_tasks[i]);
        ASSERT(!s_tasks[i].on_rq);
    }

    ASSERT_EQ(rq->nr_running, 0);
    ASSERT_EQ(rq_pick_next(rq), rq->idle);

    finish(rq);
}

TEST_CASE(sched_rr_priority_order)
{
    struct rq *rq;

    rq = reset_state();

    s_tasks[0].prio = TASK_PRIO_LOWEST;
    s_tasks[1].prio = TASK_PRIO_DEFAULT;
    s_tasks[2].prio = TASK_PRIO_HIGHEST;
    s_tasks[3].prio = TASK_PRIO_DEFAULT;

    rq_enqueue(rq, &s_tasks[0]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[0]);

    rq_enqueue(rq, &s_tasks[1]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[1]);

    rq_enqueue(rq, &s_tasks[2]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[2]);

    rq_enqueue(rq, &s_tasks[3]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[2]);

    rq_dequeue(rq, &s_tasks[2]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[1]);

    rq_dequeue(rq, &s_tasks[1]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[3]);

    rq_dequeue(rq, &s_tasks[3]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[0]);

    rq_dequeue(rq, &s_tasks[0]);
    ASSERT_EQ(rq_pick_next(rq), rq->idle);

    finish(rq);
}

TEST_CASE(sched_rr_requeue_goes_to_the_tail)
{
    struct rq *rq;

    rq = reset_state();

    rq_enqueue(rq, &s_tasks[0]);
    rq_enqueue(rq, &s_tasks[1]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[0]);

    rq_dequeue(rq, &s_tasks[0]);
    rq_enqueue(rq, &s_tasks[0]);
    ASSERT_EQ(rq_pick_next(rq), &s_tasks[1]);

    finish(rq);
}

TEST_CASE(task_alloc_and_put)
{
    struct task *task;

    g_sched_test_live_tasks = 0;
    g_sched_test_live_stacks = 0;

    task = task_alloc();
    ASSERT_NOT_NULL(task);
    ASSERT_NOT_NULL(task->stack);
    ASSERT_EQ(task->stack_size, TASK_STACK_SIZE);
    ASSERT_EQ(g_sched_test_live_tasks, 1);
    ASSERT_EQ(g_sched_test_live_stacks, 1);

    task_ref(task);
    task_unref(task);
    ASSERT_EQ(g_sched_test_live_tasks, 1);
    ASSERT_EQ(g_sched_test_live_stacks, 1);

    task_unref(task);
    ASSERT_EQ(g_sched_test_live_tasks, 0);
    ASSERT_EQ(g_sched_test_live_stacks, 0);
}
