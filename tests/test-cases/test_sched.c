#include <string.h>

#include <common/types.h>
#include <memory/alloc_behavior.h>

extern int g_sched_test_live_tasks, g_sched_test_live_stacks;

void *sched_test_alloc(size_t size, enum alloc_behavior behavior);
void sched_test_free(void *ptr);
void *sched_test_valloc(size_t size, enum alloc_behavior behavior);
void sched_test_vrelease(void *ptr);

struct timer;
enum timer_result : u32;

void sched_test_timer_init(
    struct timer *timer, enum timer_result (*fn)(struct timer *timer)
);
void sched_test_timer_arm_with_slack(
    struct timer *timer, u64 deadline_ns, u64 slack_ns
);
void sched_test_timer_cancel(struct timer *timer);
u64 sched_test_now(void);

#define timer_init sched_test_timer_init
#define timer_arm_with_slack sched_test_timer_arm_with_slack
#define timer_cancel sched_test_timer_cancel
#define ns_since_boot sched_test_now

#define alloc sched_test_alloc
#define free sched_test_free
#define valloc sched_test_valloc
#define vrelease sched_test_vrelease

#include <kernel-source/sched/task.c>
#include <kernel-source/sched/core.c>
#include <kernel-source/sched/rr.c>
#include <kernel-source/sched/idle.c>
#include <kernel-source/sched/kthread.c>
#include <kernel-source/sched/wait_queue.c>
#include <kernel-source/sched/completion.c>

#include <private/arch/task.h>
#include <private/arch/preempt.h>

void test_cpu_idle_wait(void)
{
    g_test_irqs_enabled = true;
}

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

struct waiter {
    struct wait_queue *wq;
    bool *condition;
    int id;
    bool exclusive;
};

static void wait_for_condition(void *user)
{
    struct waiter *waiter = user;

    wait_event(waiter->wq, *waiter->condition);
    log_id(waiter->id);
}

static void wait_once(void *user)
{
    struct waiter *waiter = user;
    struct wait_entry entry;
    enum wait_entry_flags flags = WAIT_ENTRY_FLAG_NONE;

    if (waiter->exclusive)
        flags = WAIT_ENTRY_FLAG_EXCLUSIVE;

    wait_entry_init(&entry, flags);
    wait_queue_prepare(waiter->wq, &entry);
    schedule();
    wait_queue_finish(waiter->wq, &entry);

    log_id(waiter->id);
}

static int count_ids(int id)
{
    int i, count = 0;

    for (i = 0; i < s_log_length; i++) {
        if (s_log[i] == id)
            count++;
    }

    return count;
}

TEST_CASE(wait_event_returns_at_once_if_true)
{
    DEFINE_WAIT_QUEUE(wq)
    bool condition = true;
    struct waiter waiter = { .wq = &wq, .condition = &condition, .id = 1 };

    switching_test_begin();
    spawn(wait_for_condition, &waiter, TASK_PRIO_DEFAULT);

    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT(list_is_empty(&wq.entries));

    switching_test_end();
}

TEST_CASE(wait_event_sleeps_until_woken_with_the_condition)
{
    DEFINE_WAIT_QUEUE(wq)
    bool condition = false;
    struct waiter waiter = { .wq = &wq, .condition = &condition, .id = 1 };
    struct task *task;

    switching_test_begin();
    task = spawn(wait_for_condition, &waiter, TASK_PRIO_DEFAULT);
    task_ref(task);

    schedule_idle();
    ASSERT_EQ(s_log_length, 0);
    ASSERT_EQ(task_state(task), TASK_STATE_UNINTERRUPTIBLE);
    ASSERT(!list_is_empty(&wq.entries));

    wait_queue_wake(&wq);
    schedule_idle();
    ASSERT_EQ(s_log_length, 0);
    ASSERT_EQ(task_state(task), TASK_STATE_UNINTERRUPTIBLE);

    condition = true;
    wait_queue_wake(&wq);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT_EQ(task_state(task), TASK_STATE_DEAD);
    ASSERT(list_is_empty(&wq.entries));

    task_unref(task);
    ASSERT_EQ(g_sched_test_live_tasks, 0);

    switching_test_end();
}

TEST_CASE(wait_queue_wake_wakes_every_plain_waiter)
{
    DEFINE_WAIT_QUEUE(wq)
    struct waiter waiters[3];
    int i;

    switching_test_begin();

    for (i = 0; i < 3; i++) {
        waiters[i] = (struct waiter) { .wq = &wq, .id = i };
        spawn(wait_once, &waiters[i], TASK_PRIO_DEFAULT);
    }

    schedule_idle();
    ASSERT_EQ(s_log_length, 0);

    wait_queue_wake(&wq);
    schedule_idle();

    ASSERT_EQ(s_log_length, 3);
    ASSERT(list_is_empty(&wq.entries));
    ASSERT_EQ(g_sched_test_live_tasks, 0);

    switching_test_end();
}

TEST_CASE(wait_queue_wake_wakes_one_exclusive_waiter)
{
    DEFINE_WAIT_QUEUE(wq)
    struct waiter waiters[3];
    int i;

    switching_test_begin();

    for (i = 0; i < 3; i++) {
        waiters[i] = (struct waiter) { .wq = &wq, .id = i, .exclusive = true };
        spawn(wait_once, &waiters[i], TASK_PRIO_DEFAULT);
    }

    schedule_idle();

    for (i = 0; i < 3; i++) {
        wait_queue_wake(&wq);
        schedule_idle();

        ASSERT_EQ(s_log_length, i + 1);
        ASSERT_EQ(s_log[i], i);
    }

    ASSERT(list_is_empty(&wq.entries));

    switching_test_end();
}

TEST_CASE(wait_queue_wake_all_ignores_exclusivity)
{
    DEFINE_WAIT_QUEUE(wq)
    struct waiter waiters[3];
    int i;

    switching_test_begin();

    for (i = 0; i < 3; i++) {
        waiters[i] = (struct waiter) { .wq = &wq, .id = i, .exclusive = true };
        spawn(wait_once, &waiters[i], TASK_PRIO_DEFAULT);
    }

    schedule_idle();

    wait_queue_wake_all(&wq);
    schedule_idle();

    ASSERT_EQ(s_log_length, 3);

    switching_test_end();
}

TEST_CASE(wait_queue_plain_waiters_come_before_exclusive_ones)
{
    DEFINE_WAIT_QUEUE(wq)
    struct waiter waiters[4];
    int i;

    switching_test_begin();

    for (i = 0; i < 4; i++) {
        waiters[i] = (struct waiter) {
            .wq = &wq, .id = i % 2, .exclusive = i % 2 == 0,
        };
        spawn(wait_once, &waiters[i], TASK_PRIO_DEFAULT);
    }

    schedule_idle();

    wait_queue_wake(&wq);
    schedule_idle();

    ASSERT_EQ(count_ids(1), 2);
    ASSERT_EQ(count_ids(0), 1);

    wait_queue_wake(&wq);
    schedule_idle();

    ASSERT_EQ(count_ids(0), 2);
    ASSERT(list_is_empty(&wq.entries));

    switching_test_end();
}

struct counting_entry {
    struct wait_entry entry;
    int calls;
    bool result;
};

static bool count_the_wake(struct wait_entry *entry)
{
    struct counting_entry *counting;

    counting = container_of(entry, struct counting_entry, entry);
    counting->calls++;

    return counting->result;
}

TEST_CASE(wait_queue_entry_with_its_own_callback)
{
    DEFINE_WAIT_QUEUE(wq)
    struct counting_entry first = { .result = false };
    struct counting_entry second = { .result = true };

    switching_test_begin();

    first.entry.flags = WAIT_ENTRY_FLAG_EXCLUSIVE;
    first.entry.wake = count_the_wake;
    second.entry.flags = WAIT_ENTRY_FLAG_EXCLUSIVE;
    second.entry.wake = count_the_wake;

    list_insert_prev(&wq.entries, &first.entry.link);
    list_insert_prev(&wq.entries, &second.entry.link);

    wait_queue_wake(&wq);
    ASSERT_EQ(first.calls, 1);
    ASSERT_EQ(second.calls, 1);

    second.result = false;
    first.result = true;

    wait_queue_wake(&wq);
    ASSERT_EQ(first.calls, 2);
    ASSERT_EQ(second.calls, 1);

    switching_test_end();
}

struct completion_waiter {
    struct completion *completion;
    int id;
};

static void wait_then_log(void *user)
{
    struct completion_waiter *waiter = user;

    wait_for_completion(waiter->completion);
    log_id(waiter->id);
}

TEST_CASE(completion_done_before_the_wait)
{
    DEFINE_COMPLETION(completion)
    struct completion_waiter waiter = { .completion = &completion, .id = 1 };

    switching_test_begin();

    complete(&completion);
    ASSERT_EQ(completion.done, 1);

    spawn(wait_then_log, &waiter, TASK_PRIO_DEFAULT);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT_EQ(completion.done, 0);

    switching_test_end();
}

TEST_CASE(completion_wakes_one_waiter_per_complete)
{
    DEFINE_COMPLETION(completion)
    struct completion_waiter waiters[3];
    int i;

    switching_test_begin();

    for (i = 0; i < 3; i++) {
        waiters[i] = (struct completion_waiter) {
            .completion = &completion, .id = i,
        };
        spawn(wait_then_log, &waiters[i], TASK_PRIO_DEFAULT);
    }

    schedule_idle();
    ASSERT_EQ(s_log_length, 0);

    for (i = 0; i < 3; i++) {
        complete(&completion);
        schedule_idle();

        ASSERT_EQ(s_log_length, i + 1);
        ASSERT_EQ(s_log[i], i);
        ASSERT_EQ(completion.done, 0);
    }

    ASSERT(list_is_empty(&completion.wq.entries));
    ASSERT_EQ(g_sched_test_live_tasks, 0);

    switching_test_end();
}

TEST_CASE(completion_complete_all_stays_done)
{
    DEFINE_COMPLETION(completion)
    struct completion_waiter waiters[3];
    struct completion_waiter late = { .completion = &completion, .id = 9 };
    int i;

    switching_test_begin();

    for (i = 0; i < 3; i++) {
        waiters[i] = (struct completion_waiter) {
            .completion = &completion, .id = i,
        };
        spawn(wait_then_log, &waiters[i], TASK_PRIO_DEFAULT);
    }

    schedule_idle();

    complete_all(&completion);
    schedule_idle();
    ASSERT_EQ(s_log_length, 3);

    spawn(wait_then_log, &late, TASK_PRIO_DEFAULT);
    schedule_idle();
    ASSERT_EQ(s_log_length, 4);
    ASSERT_EQ(s_log[3], 9);

    switching_test_end();
}

TEST_CASE(completion_from_interrupt_context)
{
    DEFINE_COMPLETION(completion)
    struct completion_waiter waiter = { .completion = &completion, .id = 1 };
    struct registers regs = { .irq_state = false };
    irq_state_t irq_state;

    switching_test_begin();

    spawn(wait_then_log, &waiter, TASK_PRIO_DEFAULT);
    schedule_idle();
    ASSERT_EQ(s_log_length, 0);

    irq_state = local_irq_state_save();
    hard_irq_enter();
    complete(&completion);
    hard_irq_exit(&regs);
    local_irq_state_restore(irq_state);

    ASSERT(need_resched());
    schedule_idle();
    ASSERT_EQ(s_log_length, 1);

    switching_test_end();
}

struct worker {
    int id;
    int iterations;
};

static void work_until_stopped(void *user)
{
    struct worker *worker = user;

    for (;;) {
        set_current_state(TASK_STATE_UNINTERRUPTIBLE);

        if (kthread_should_stop())
            break;

        worker->iterations++;
        schedule();
    }

    set_current_state(TASK_STATE_RUNNING);
    log_id(worker->id);
}

struct stopper {
    struct task *target;
    int id;
};

static void stop_target(void *user)
{
    struct stopper *stopper = user;

    kthread_stop(stopper->target);
    log_id(stopper->id);
}

TEST_CASE(kthread_stop_waits_for_a_sleeping_thread)
{
    struct worker worker = { .id = 1 };
    struct stopper stopper = { .id = 2 };

    switching_test_begin();

    stopper.target = kthread_create(work_until_stopped, &worker, "worker");
    kthread_start(stopper.target);

    schedule_idle();
    ASSERT_EQ(worker.iterations, 1);
    ASSERT_EQ(s_log_length, 0);

    ASSERT_EQ(kthread_run(stop_target, &stopper, "stopper"), EOK);
    schedule_idle();

    ASSERT_EQ(s_log_length, 2);
    ASSERT_EQ(s_log[0], 1);
    ASSERT_EQ(s_log[1], 2);
    ASSERT_EQ(g_sched_test_live_tasks, 0);
    ASSERT_EQ(g_sched_test_live_stacks, 0);

    switching_test_end();
}

TEST_CASE(kthread_stop_before_start_never_runs_it)
{
    struct runner runner = { .id = 1, .rounds = 3 };
    struct stopper stopper = { .id = 2 };

    switching_test_begin();

    stopper.target = kthread_create(run_to_completion, &runner, "unstarted");

    ASSERT_EQ(kthread_run(stop_target, &stopper, "stopper"), EOK);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT_EQ(s_log[0], 2);
    ASSERT_EQ(g_sched_test_live_tasks, 0);
    ASSERT_EQ(g_sched_test_live_stacks, 0);

    switching_test_end();
}

TEST_CASE(kthread_stop_after_it_exited)
{
    struct runner runner = { .id = 1, .rounds = 1 };
    struct stopper stopper = { .id = 2 };

    switching_test_begin();

    stopper.target = kthread_create(run_to_completion, &runner, "short");
    kthread_start(stopper.target);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT_EQ(task_state(stopper.target), TASK_STATE_DEAD);

    ASSERT_EQ(kthread_run(stop_target, &stopper, "stopper"), EOK);
    schedule_idle();

    ASSERT_EQ(s_log_length, 2);
    ASSERT_EQ(s_log[1], 2);
    ASSERT_EQ(g_sched_test_live_tasks, 0);

    switching_test_end();
}

#define MAX_PENDING_TIMERS 4

static struct timer *s_pending_timers[MAX_PENDING_TIMERS];
static u64 s_now;

void sched_test_timer_init(
    struct timer *timer, enum timer_result (*fn)(struct timer *timer)
)
{
    *timer = (struct timer) {
        .fn = fn,
    };
}

void sched_test_timer_cancel(struct timer *timer)
{
    size_t i;

    for (i = 0; i < MAX_PENDING_TIMERS; i++) {
        if (s_pending_timers[i] == timer)
            s_pending_timers[i] = nullptr;
    }
}

void sched_test_timer_arm_with_slack(
    struct timer *timer, u64 deadline_ns, u64 slack_ns
)
{
    size_t i;

    sched_test_timer_cancel(timer);

    timer->deadline_ns = deadline_ns;
    timer->hard_deadline_ns = deadline_ns + slack_ns;

    for (i = 0; i < MAX_PENDING_TIMERS; i++) {
        if (s_pending_timers[i] != nullptr)
            continue;

        s_pending_timers[i] = timer;
        return;
    }

    BUG();
}

u64 sched_test_now(void)
{
    return s_now;
}

static int count_pending_timers(void)
{
    int count = 0;
    size_t i;

    for (i = 0; i < MAX_PENDING_TIMERS; i++) {
        if (s_pending_timers[i] != nullptr)
            count++;
    }

    return count;
}

// Moves the clock and runs what became due
static void advance_time_to(u64 now)
{
    struct timer *timer;
    struct registers regs = { .irq_state = false };
    irq_state_t irq_state;
    size_t i;

    irq_state = local_irq_state_save();
    hard_irq_enter();

    s_now = now;

    for (i = 0; i < MAX_PENDING_TIMERS; i++) {
        timer = s_pending_timers[i];
        if (timer == nullptr || timer->deadline_ns > now)
            continue;

        s_pending_timers[i] = nullptr;
        timer->fn(timer);
    }

    hard_irq_exit(&regs);
    local_irq_state_restore(irq_state);
}

static void timed_test_begin(void)
{
    switching_test_begin();

    memset(s_pending_timers, 0, sizeof(s_pending_timers));
    s_now = 1000;
}

struct timed_sleeper {
    int id;
    u64 ns;
    struct wait_queue *wq;
    struct completion *completion;
    bool *condition;
    bool result;
};

static void sleep_then_log(void *user)
{
    struct timed_sleeper *sleeper = user;

    log_id(sleeper->id);
    sleep_ns(sleeper->ns);
    log_id(sleeper->id);
}

TEST_CASE(sleep_ns_sleeps_until_the_deadline)
{
    struct timed_sleeper sleeper = { .id = 1, .ns = 100 };

    timed_test_begin();
    spawn(sleep_then_log, &sleeper, TASK_PRIO_DEFAULT);

    schedule_idle();
    ASSERT_EQ(s_log_length, 1);
    ASSERT_EQ(count_pending_timers(), 1);

    advance_time_to(1099);
    ASSERT(!need_resched());
    schedule_idle();
    ASSERT_EQ(s_log_length, 1);

    advance_time_to(1100);
    ASSERT(need_resched());
    schedule_idle();
    ASSERT_EQ(s_log_length, 2);

    ASSERT_EQ(count_pending_timers(), 0);
    ASSERT_EQ(g_sched_test_live_tasks, 0);

    switching_test_end();
}

TEST_CASE(sleep_ns_goes_back_to_sleep_after_an_early_wakeup)
{
    struct timed_sleeper sleeper = { .id = 1, .ns = 100 };
    struct task *task;

    timed_test_begin();
    task = spawn(sleep_then_log, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();

    advance_time_to(1050);
    ASSERT(wake_up_task(task, TASK_STATE_UNINTERRUPTIBLE));
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT_EQ(count_pending_timers(), 1);

    advance_time_to(1100);
    schedule_idle();
    ASSERT_EQ(s_log_length, 2);
    ASSERT_EQ(count_pending_timers(), 0);

    switching_test_end();
}

static void wait_event_with_timeout(void *user)
{
    struct timed_sleeper *sleeper = user;

    sleeper->result = wait_event_timeout(
        sleeper->wq, *sleeper->condition, sleeper->ns
    );
    log_id(sleeper->id);
}

TEST_CASE(wait_event_timeout_gives_up_at_the_deadline)
{
    DEFINE_WAIT_QUEUE(wq)
    bool condition = false;
    struct timed_sleeper sleeper = {
        .id = 1, .ns = 100, .wq = &wq, .condition = &condition,
        .result = true,
    };

    timed_test_begin();
    spawn(wait_event_with_timeout, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();
    ASSERT_EQ(s_log_length, 0);

    wait_queue_wake_all(&wq);
    schedule_idle();
    ASSERT_EQ(s_log_length, 0);

    advance_time_to(1100);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT(!sleeper.result);
    ASSERT(list_is_empty(&wq.entries));
    ASSERT_EQ(count_pending_timers(), 0);

    switching_test_end();
}

TEST_CASE(wait_event_timeout_returns_when_the_condition_holds)
{
    DEFINE_WAIT_QUEUE(wq)
    bool condition = false;
    struct timed_sleeper sleeper = {
        .id = 1, .ns = 100, .wq = &wq, .condition = &condition,
    };

    timed_test_begin();
    spawn(wait_event_with_timeout, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();

    advance_time_to(1050);
    condition = true;
    wait_queue_wake_all(&wq);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT(sleeper.result);
    ASSERT(list_is_empty(&wq.entries));
    ASSERT_EQ(count_pending_timers(), 0);

    switching_test_end();
}

TEST_CASE(wait_event_timeout_takes_a_condition_met_at_the_deadline)
{
    DEFINE_WAIT_QUEUE(wq)
    bool condition = false;
    struct timed_sleeper sleeper = {
        .id = 1, .ns = 100, .wq = &wq, .condition = &condition,
    };

    timed_test_begin();
    spawn(wait_event_with_timeout, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();

    condition = true;
    advance_time_to(1100);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT(sleeper.result);

    switching_test_end();
}

TEST_CASE(wait_event_timeout_without_a_deadline_arms_no_timer)
{
    DEFINE_WAIT_QUEUE(wq)
    bool condition = false;
    struct timed_sleeper sleeper = {
        .id = 1, .ns = UNSIGNED_MAX(u64), .wq = &wq, .condition = &condition,
    };

    timed_test_begin();
    spawn(wait_event_with_timeout, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();

    ASSERT_EQ(s_log_length, 0);
    ASSERT_EQ(count_pending_timers(), 0);

    condition = true;
    wait_queue_wake_all(&wq);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT(sleeper.result);

    switching_test_end();
}

static void wait_for_completion_with_timeout(void *user)
{
    struct timed_sleeper *sleeper = user;

    sleeper->result = wait_for_completion_timeout(
        sleeper->completion, sleeper->ns
    );
    log_id(sleeper->id);
}

TEST_CASE(completion_timeout_gives_up_at_the_deadline)
{
    DEFINE_COMPLETION(completion)
    struct timed_sleeper sleeper = {
        .id = 1, .ns = 100, .completion = &completion, .result = true,
    };

    timed_test_begin();
    spawn(wait_for_completion_with_timeout, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();
    ASSERT_EQ(s_log_length, 0);

    advance_time_to(1100);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT(!sleeper.result);
    ASSERT(list_is_empty(&completion.wq.entries));
    ASSERT_EQ(count_pending_timers(), 0);

    complete(&completion);
    ASSERT_EQ(completion.done, 1);

    switching_test_end();
}

TEST_CASE(completion_timeout_returns_when_completed)
{
    DEFINE_COMPLETION(completion)
    struct timed_sleeper sleeper = {
        .id = 1, .ns = 100, .completion = &completion,
    };

    timed_test_begin();
    spawn(wait_for_completion_with_timeout, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();

    advance_time_to(1050);
    complete(&completion);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT(sleeper.result);
    ASSERT_EQ(completion.done, 0);
    ASSERT_EQ(count_pending_timers(), 0);

    switching_test_end();
}

TEST_CASE(completion_timeout_takes_a_complete_at_the_deadline)
{
    DEFINE_COMPLETION(completion)
    struct timed_sleeper sleeper = {
        .id = 1, .ns = 100, .completion = &completion,
    };

    timed_test_begin();
    spawn(wait_for_completion_with_timeout, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();

    complete(&completion);
    advance_time_to(1100);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT(sleeper.result);
    ASSERT_EQ(completion.done, 0);
    ASSERT_EQ(count_pending_timers(), 0);

    switching_test_end();
}

TEST_CASE(completion_timeout_does_not_wait_when_already_done)
{
    DEFINE_COMPLETION(completion)
    struct timed_sleeper sleeper = {
        .id = 1, .ns = 100, .completion = &completion,
    };

    timed_test_begin();
    complete(&completion);

    spawn(wait_for_completion_with_timeout, &sleeper, TASK_PRIO_DEFAULT);
    schedule_idle();

    ASSERT_EQ(s_log_length, 1);
    ASSERT(sleeper.result);
    ASSERT_EQ(count_pending_timers(), 0);

    switching_test_end();
}
