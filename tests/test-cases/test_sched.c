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
#include <kernel-source/sched/kthread.c>

#include <private/arch/task.h>
#include <private/arch/preempt.h>

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

#define MAX_LOG 64

struct runner {
    int id;
    int rounds;
};

static int s_log[MAX_LOG];
static int s_log_length;

static void log_id(int id)
{
    if (s_log_length < MAX_LOG)
        s_log[s_log_length++] = id;
}

static struct rq *switching_test_begin(void)
{
    g_num_present_cpus = 1;
    g_per_cpu_offset[0] = 0;

    memset(&g_this_cpu_rq, 0, sizeof(g_this_cpu_rq));
    sched_init();

    g_test_current = &g_this_cpu_idle_task;
    current->preempt_count = PREEMPT_DISABLE_DEPTH(1);
    task_clear_flag(current, TASK_FLAG_NEED_RESCHED);
    g_test_irqs_enabled = true;
    g_this_cpu_idle_task.flags = 0;

    g_sched_test_live_tasks = 0;
    g_sched_test_live_stacks = 0;
    s_log_length = 0;

    return cpu_rq(0);
}

static void switching_test_end(void)
{
    current->preempt_count = 0;
}

static struct task *spawn(task_fn_t fn, void *user, u8 prio)
{
    struct task *task;

    task = task_alloc();
    task->prio = prio;
    task->sched_class = &SCHED_CLASS_INSTANCE(rr);
    arch_task_frame_init(task, fn, user);

    wake_up_task(task, TASK_STATE_NEW);
    task_unref(task);
    return task;
}

static void yield_between_rounds(void *user)
{
    struct runner *runner = user;
    int i;

    for (i = 0; i < runner->rounds; i++) {
        log_id(runner->id);
        yield();
    }
}

static void run_to_completion(void *user)
{
    struct runner *runner = user;
    int i;

    for (i = 0; i < runner->rounds; i++)
        log_id(runner->id);
}

static void log_and_sleep(void *user)
{
    struct runner *runner = user;

    for (;;) {
        log_id(runner->id);
        set_current_state(TASK_STATE_UNINTERRUPTIBLE);
        schedule();
    }
}

TEST_CASE(sched_idle_with_nothing_to_run)
{
    struct rq *rq;

    rq = switching_test_begin();

    schedule_idle();

    ASSERT_EQ(current, rq->idle);
    ASSERT_EQ(rq->nr_switches, 0);
    ASSERT_EQ(preempt_count(), PREEMPT_DISABLE_DEPTH(1));
    ASSERT(local_irq_enabled());

    switching_test_end();
}

TEST_CASE(sched_runs_a_task_until_it_exits)
{
    struct runner runner = { .id = 7, .rounds = 3 };
    struct rq *rq;

    rq = switching_test_begin();
    spawn(run_to_completion, &runner, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT_EQ(current, rq->idle);
    ASSERT_EQ(rq->curr, rq->idle);
    ASSERT_EQ(s_log_length, 3);
    ASSERT_EQ(rq->nr_running, 0);
    ASSERT_EQ(rq->nr_switches, 2);
    ASSERT_EQ(g_sched_test_live_tasks, 0);
    ASSERT_EQ(g_sched_test_live_stacks, 0);
    ASSERT_EQ(preempt_count(), PREEMPT_DISABLE_DEPTH(1));
    ASSERT(local_irq_enabled());
    ASSERT(!spin_is_locked(&rq->lock));

    switching_test_end();
}

TEST_CASE(sched_yield_round_robins)
{
    struct runner runners[3];
    struct rq *rq;
    int i;

    rq = switching_test_begin();

    for (i = 0; i < 3; i++) {
        runners[i] = (struct runner) { .id = i, .rounds = 3 };
        spawn(yield_between_rounds, &runners[i], TASK_PRIO_DEFAULT);
    }

    schedule_idle();

    ASSERT_EQ(s_log_length, 9);
    for (i = 0; i < 9; i++)
        ASSERT_EQ(s_log[i], i % 3);

    ASSERT_EQ(rq->nr_running, 0);
    ASSERT_EQ(g_sched_test_live_tasks, 0);
    ASSERT_EQ(g_sched_test_live_stacks, 0);

    switching_test_end();
}

TEST_CASE(sched_higher_priority_runs_first)
{
    struct runner low = { .id = 1, .rounds = 2 };
    struct runner high = { .id = 2, .rounds = 2 };
    struct rq *rq;

    rq = switching_test_begin();

    spawn(yield_between_rounds, &low, TASK_PRIO_LOWEST);
    spawn(yield_between_rounds, &high, TASK_PRIO_HIGHEST);

    schedule_idle();

    ASSERT_EQ(s_log_length, 4);
    ASSERT_EQ(s_log[0], 2);
    ASSERT_EQ(s_log[1], 2);
    ASSERT_EQ(s_log[2], 1);
    ASSERT_EQ(s_log[3], 1);
    ASSERT_EQ(rq->nr_running, 0);

    switching_test_end();
}

TEST_CASE(sched_sleeping_task_leaves_the_runqueue)
{
    struct runner sleeper = { .id = 1 };
    struct runner other = { .id = 2, .rounds = 2 };
    struct task *task;
    struct rq *rq;

    rq = switching_test_begin();

    task = spawn(log_and_sleep, &sleeper, TASK_PRIO_HIGHEST);
    spawn(run_to_completion, &other, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT_EQ(s_log_length, 3);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 2);
    ASSERT_EQ(s_log[2], 2);

    ASSERT(!task->on_rq);
    ASSERT_EQ(task_state(task), TASK_STATE_UNINTERRUPTIBLE);
    ASSERT_EQ(rq->nr_running, 0);
    ASSERT_EQ(g_sched_test_live_tasks, 1);

    task_unref(task);
    switching_test_end();
}

struct sleeper {
    struct task *task;
    struct task *to_wake;
    int id;
    bool woke_self;
    bool wake_result;
};

static void sleep_once(void *user)
{
    struct sleeper *sleeper = user;

    log_id(sleeper->id);

    set_current_state(TASK_STATE_UNINTERRUPTIBLE);
    schedule();

    log_id(sleeper->id);
}

static void wake_other(void *user)
{
    struct sleeper *sleeper = user;

    log_id(sleeper->id);
    sleeper->wake_result = wake_up_task(
        sleeper->to_wake, TASK_STATE_UNINTERRUPTIBLE
    );
    log_id(sleeper->id);
}

static void wake_self_before_schedule(void *user)
{
    struct sleeper *sleeper = user;

    set_current_state(TASK_STATE_UNINTERRUPTIBLE);
    sleeper->woke_self = wake_up_task(current, TASK_STATE_UNINTERRUPTIBLE);
    schedule();

    log_id(sleeper->id);
}

TEST_CASE(sched_new_task_is_woken_once)
{
    struct runner runner = { .id = 1, .rounds = 1 };
    struct task *task;
    struct rq *rq;

    rq = switching_test_begin();

    task = spawn(run_to_completion, &runner, TASK_PRIO_DEFAULT);
    ASSERT(task->on_rq);
    ASSERT_EQ(task_state(task), TASK_STATE_RUNNING);
    ASSERT_EQ(rq->nr_running, 1);

    ASSERT(!wake_up_task(task, TASK_STATE_NEW));
    ASSERT(!wake_up_task(task, TASK_STATE_UNINTERRUPTIBLE));
    ASSERT_EQ(rq->nr_running, 1);

    schedule_idle();
    ASSERT_EQ(s_log_length, 1);

    switching_test_end();
}

TEST_CASE(sched_wakeup_resumes_a_sleeper)
{
    struct sleeper sleeper = { .id = 1 };
    struct sleeper waker = { .id = 2 };
    struct rq *rq;

    rq = switching_test_begin();

    sleeper.task = spawn(sleep_once, &sleeper, TASK_PRIO_DEFAULT);
    waker.to_wake = sleeper.task;
    waker.task = spawn(wake_other, &waker, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT(waker.wake_result);
    ASSERT_EQ(s_log_length, 4);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 2);
    ASSERT_EQ(s_log[2], 2);
    ASSERT_EQ(s_log[3], 1);

    ASSERT_EQ(rq->nr_running, 0);
    ASSERT_EQ(g_sched_test_live_tasks, 0);

    switching_test_end();
}

TEST_CASE(sched_wakeup_before_schedule_is_not_lost)
{
    struct sleeper sleeper = { .id = 1 };
    struct rq *rq;

    rq = switching_test_begin();

    spawn(wake_self_before_schedule, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();

    ASSERT(sleeper.woke_self);
    ASSERT_EQ(s_log_length, 1);
    ASSERT_EQ(rq->nr_running, 0);
    ASSERT_EQ(g_sched_test_live_tasks, 0);

    switching_test_end();
}

struct spawner {
    int id;
    u8 child_prio;
    bool disable_preemption;
    struct runner child;
};

static void spawn_child_between_logs(void *user)
{
    struct spawner *spawner = user;

    log_id(spawner->id);

    if (spawner->disable_preemption)
        preempt_disable();

    spawn(run_to_completion, &spawner->child, spawner->child_prio);
    log_id(spawner->id);

    if (spawner->disable_preemption)
        preempt_enable();

    log_id(spawner->id);
}

TEST_CASE(sched_waking_idle_cpu_requests_a_reschedule)
{
    struct runner runner = { .id = 1, .rounds = 1 };
    struct rq *rq;

    rq = switching_test_begin();
    ASSERT(!need_resched());

    spawn(run_to_completion, &runner, TASK_PRIO_DEFAULT);
    ASSERT(need_resched());
    ASSERT_EQ(current, rq->idle);

    schedule_idle();
    ASSERT(!need_resched());
    ASSERT_EQ(s_log_length, 1);

    switching_test_end();
}

TEST_CASE(sched_higher_priority_wakeup_preempts)
{
    struct spawner spawner = {
        .id = 1,
        .child_prio = TASK_PRIO_HIGHEST,
        .child = { .id = 2, .rounds = 1 },
    };

    switching_test_begin();
    spawn(spawn_child_between_logs, &spawner, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT_EQ(s_log_length, 4);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 2);
    ASSERT_EQ(s_log[2], 1);
    ASSERT_EQ(s_log[3], 1);
    ASSERT_EQ(g_sched_test_live_tasks, 0);

    switching_test_end();
}

TEST_CASE(sched_equal_priority_wakeup_does_not_preempt)
{
    struct spawner spawner = {
        .id = 1,
        .child_prio = TASK_PRIO_DEFAULT,
        .child = { .id = 2, .rounds = 1 },
    };

    switching_test_begin();
    spawn(spawn_child_between_logs, &spawner, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT_EQ(s_log_length, 4);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 1);
    ASSERT_EQ(s_log[2], 1);
    ASSERT_EQ(s_log[3], 2);

    switching_test_end();
}

TEST_CASE(sched_preemption_waits_for_preempt_enable)
{
    struct spawner spawner = {
        .id = 1,
        .child_prio = TASK_PRIO_HIGHEST,
        .disable_preemption = true,
        .child = { .id = 2, .rounds = 1 },
    };

    switching_test_begin();
    spawn(spawn_child_between_logs, &spawner, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT_EQ(s_log_length, 4);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 1);
    ASSERT_EQ(s_log[2], 2);
    ASSERT_EQ(s_log[3], 1);

    switching_test_end();
}

struct preempted_sleeper {
    struct task *task;
    struct runner unused;
    bool woken_by_child;
};

static void wake_parent(void *user)
{
    struct preempted_sleeper *sleeper = user;

    log_id(2);
    sleeper->woken_by_child = wake_up_task(
        sleeper->task, TASK_STATE_UNINTERRUPTIBLE
    );
}

static void get_preempted_while_going_to_sleep(void *user)
{
    struct preempted_sleeper *sleeper = user;

    log_id(1);

    set_current_state(TASK_STATE_UNINTERRUPTIBLE);
    spawn(wake_parent, sleeper, TASK_PRIO_HIGHEST);
    schedule();

    log_id(1);
}

TEST_CASE(sched_preempted_sleeper_stays_queued)
{
    struct preempted_sleeper sleeper = { 0 };
    struct rq *rq;

    rq = switching_test_begin();

    sleeper.task = spawn(
        get_preempted_while_going_to_sleep, &sleeper, TASK_PRIO_DEFAULT
    );
    schedule_idle();

    ASSERT(sleeper.woken_by_child);
    ASSERT_EQ(s_log_length, 3);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 2);
    ASSERT_EQ(s_log[2], 1);
    ASSERT_EQ(rq->nr_running, 0);
    ASSERT_EQ(g_sched_test_live_tasks, 0);

    switching_test_end();
}

struct interrupted {
    int id;
    bool irqs_were_enabled;
    bool preemption_disabled;
    struct runner child;
};

/*
 * Emulates a handler that wakes a task: the wakeup runs in hard
 * interrupt context with interrupts off, then the exit path decides
 * whether to switch.
 */
static void take_fake_interrupt(struct interrupted *ctx)
{
    struct registers regs = { .irq_state = ctx->irqs_were_enabled };
    irq_state_t irq_state;

    irq_state = local_irq_state_save();
    hard_irq_enter();
    spawn(run_to_completion, &ctx->child, TASK_PRIO_HIGHEST);
    hard_irq_exit(&regs);

    local_irq_state_restore(irq_state);
}

static void get_interrupted(void *user)
{
    struct interrupted *ctx = user;
    irq_state_t irq_state = 0;

    log_id(ctx->id);

    if (ctx->preemption_disabled)
        preempt_disable();
    if (!ctx->irqs_were_enabled)
        irq_state = local_irq_state_save();

    take_fake_interrupt(ctx);
    log_id(ctx->id);

    if (!ctx->irqs_were_enabled)
        local_irq_state_restore(irq_state);
    if (ctx->preemption_disabled)
        preempt_enable();

    log_id(ctx->id);
}

TEST_CASE(sched_interrupt_exit_preempts)
{
    struct interrupted ctx = {
        .id = 1,
        .irqs_were_enabled = true,
        .child = { .id = 2, .rounds = 1 },
    };

    switching_test_begin();
    spawn(get_interrupted, &ctx, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT_EQ(s_log_length, 4);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 2);
    ASSERT_EQ(s_log[2], 1);
    ASSERT_EQ(s_log[3], 1);
    ASSERT(local_irq_enabled());

    switching_test_end();
}

TEST_CASE(sched_interrupt_exit_respects_preempt_disable)
{
    struct interrupted ctx = {
        .id = 1,
        .irqs_were_enabled = true,
        .preemption_disabled = true,
        .child = { .id = 2, .rounds = 1 },
    };

    switching_test_begin();
    spawn(get_interrupted, &ctx, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT_EQ(s_log_length, 4);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 1);
    ASSERT_EQ(s_log[2], 2);
    ASSERT_EQ(s_log[3], 1);

    switching_test_end();
}

TEST_CASE(sched_interrupt_exit_respects_disabled_interrupts)
{
    struct interrupted ctx = {
        .id = 1,
        .irqs_were_enabled = false,
        .child = { .id = 2, .rounds = 1 },
    };

    switching_test_begin();
    spawn(get_interrupted, &ctx, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT_EQ(s_log_length, 4);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 1);
    ASSERT_EQ(s_log[2], 1);
    ASSERT_EQ(s_log[3], 2);

    switching_test_end();
}

TEST_CASE(kthread_create_then_start)
{
    struct runner runner = { .id = 5, .rounds = 2 };
    struct task *task;
    struct rq *rq;

    rq = switching_test_begin();

    task = kthread_create(run_to_completion, &runner, "worker/%d", 3);
    ASSERT_NOT_NULL(task);
    ASSERT_STR_EQ(task->name, "worker/3");
    ASSERT_EQ(task_state(task), TASK_STATE_NEW);
    ASSERT_EQ(task->prio, TASK_PRIO_DEFAULT);
    ASSERT_EQ(task->sched_class, &SCHED_CLASS_INSTANCE(rr));
    ASSERT(!task->on_rq);
    ASSERT_EQ(rq->nr_running, 0);

    schedule_idle();
    ASSERT_EQ(s_log_length, 0);

    kthread_start(task);
    ASSERT_EQ(rq->nr_running, 1);

    schedule_idle();
    ASSERT_EQ(s_log_length, 2);
    ASSERT_EQ(task_state(task), TASK_STATE_DEAD);
    ASSERT_EQ(g_sched_test_live_tasks, 1);

    task_unref(task);
    ASSERT_EQ(g_sched_test_live_tasks, 0);
    ASSERT_EQ(g_sched_test_live_stacks, 0);

    switching_test_end();
}

TEST_CASE(kthread_run_runs_and_frees)
{
    struct runner runner = { .id = 9, .rounds = 3 };

    switching_test_begin();

    ASSERT_EQ(kthread_run(run_to_completion, &runner, "runner"), EOK);
    ASSERT_EQ(g_sched_test_live_tasks, 1);

    schedule_idle();

    ASSERT_EQ(s_log_length, 3);
    ASSERT_EQ(s_log[0], 9);
    ASSERT_EQ(g_sched_test_live_tasks, 0);
    ASSERT_EQ(g_sched_test_live_stacks, 0);

    switching_test_end();
}
