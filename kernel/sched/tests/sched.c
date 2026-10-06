#include <ktest/ktest.h>

#include <private/sched/task.h>
#include <private/sched/sched.h>

#include <common/atomic.h>

#include <sched/kthread.h>
#include <sched/sched.h>
#include <sched/wait_queue.h>
#include <time/time.h>
#include <time/units.h>

KTEST_CASE(sleep_returns_no_earlier_than_asked)
{
    static const u64 s_durations_ns[] = {
        NS_PER_MS, 5 * NS_PER_MS, 20 * NS_PER_MS,
    };
    u64 before, after;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(s_durations_ns); i++) {
        before = ns_since_boot();
        sleep_ns(s_durations_ns[i]);
        after = ns_since_boot();

        KTEST_EXPECT_GE(after - before, s_durations_ns[i]);
    }
}

#define NUM_SPINNERS 3
#define SPIN_LIMIT_NS NS_PER_SEC

struct spinners {
    u32 last;
    u32 num_saw_another;
    bool saw_another[NUM_SPINNERS];
};

static void spin(void *user, u32 index)
{
    struct spinners *s = user;
    bool saw_another = false;
    u64 deadline;
    u32 me, last;

    me = index + 1;
    deadline = ns_from_now(SPIN_LIMIT_NS);

    for (;;) {
        if (atomic_load_relaxed(&s->num_saw_another) == NUM_SPINNERS)
            break;
        if (ns_since_boot() > deadline)
            break;

        last = atomic_xchg(&s->last, me, MO_RELAXED);
        if (last == me || last == 0)
            continue;
        if (saw_another)
            continue;

        saw_another = true;
        atomic_fetch_add(&s->num_saw_another, 1, MO_RELAXED);
    }

    s->saw_another[index] = saw_another;
}

KTEST_CASE(equal_priority_spinners_share_the_cpu)
{
    struct spinners s = { 0 };
    u32 i, num_started;

    num_started = KTEST_RUN_THREADS(spin, &s, NUM_SPINNERS, "spinner");

    for (i = 0; i < num_started; i++)
        KTEST_EXPECT(s.saw_another[i]);
}

static void set_flag(void *user)
{
    atomic_store_release((bool*)user, true);
}

static struct task *create_flag_setter(bool *flag, u8 prio)
{
    struct task *task;

    task = kthread_create(set_flag, flag, "flag-setter");
    if (task != nullptr)
        task->prio = prio;

    return task;
}

KTEST_CASE(higher_priority_wakeup_preempts_at_once)
{
    struct task *task;
    bool flag = false;

    task = create_flag_setter(&flag, TASK_PRIO_HIGHEST);
    KTEST_ASSERT(task != nullptr);

    kthread_start(task);
    KTEST_EXPECT(atomic_load_acquire(&flag));

    task_unref(task);
}

KTEST_CASE(equal_priority_wakeup_does_not_preempt)
{
    struct task *task;
    bool flag = false;

    sleep_ns(NS_PER_MS);

    task = create_flag_setter(&flag, TASK_PRIO_DEFAULT);
    KTEST_ASSERT(task != nullptr);

    kthread_start(task);
    KTEST_EXPECT(!atomic_load_acquire(&flag));

    yield();
    KTEST_EXPECT(atomic_load_acquire(&flag));

    task_unref(task);
}

static void sleep_until_stopped(void *user)
{
    DEFINE_WAIT_QUEUE(wq)
    bool *ran = user;

    wait_event(&wq, kthread_should_stop());
    *ran = true;
}

KTEST_CASE(kthread_stop_wakes_a_sleeping_thread)
{
    struct task *task;
    bool ran = false;

    task = kthread_create(sleep_until_stopped, &ran, "sleeper");
    KTEST_ASSERT(task != nullptr);
    kthread_start(task);

    while (task_state(task) != TASK_STATE_UNINTERRUPTIBLE)
        yield();

    kthread_stop(task);
    KTEST_EXPECT(ran);
}

KTEST_CASE(kthread_stop_before_start_never_runs_it)
{
    struct task *task;
    bool ran = false;

    task = kthread_create(sleep_until_stopped, &ran, "never-started");
    KTEST_ASSERT(task != nullptr);

    kthread_stop(task);
    yield();

    KTEST_EXPECT(!ran);
}
