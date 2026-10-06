#include <ktest/ktest.h>

#include <private/sched/sched.h>

#include <common/atomic.h>

#include <locking/mutex.h>
#include <sched/kthread.h>
#include <sched/sched.h>
#include <sched/completion.h>
#include <time/units.h>

#define NUM_CONTENDERS 4
#define ITERATIONS_PER_CONTENDER 200

struct contention {
    struct mutex mutex;
    u32 inside;
    u32 violations;
    u64 total;
};

static void contend(void *user, u32 index)
{
    struct contention *c = user;
    u32 i;

    UNREFERENCED_PARAMETER(index);

    for (i = 0; i < ITERATIONS_PER_CONTENDER; i++) {
        mutex_lock(&c->mutex);

        if (atomic_fetch_add(&c->inside, 1, MO_RELAXED) != 0)
            atomic_fetch_add(&c->violations, 1, MO_RELAXED);

        if (i % 50 == 0)
            sleep_ns(NS_PER_MS);

        c->total++;

        atomic_fetch_sub(&c->inside, 1, MO_RELAXED);
        mutex_unlock(&c->mutex);
    }
}

KTEST_CASE(contended_lock_excludes)
{
    struct contention c = { 0 };
    u32 num_started;

    mutex_init(&c.mutex);

    num_started = KTEST_RUN_THREADS(contend, &c, NUM_CONTENDERS, "contender");

    KTEST_EXPECT_EQ(c.violations, 0);
    KTEST_EXPECT_EQ(c.total, num_started * ITERATIONS_PER_CONTENDER);
    KTEST_EXPECT(mutex_try_lock(&c.mutex));
    mutex_unlock(&c.mutex);
}

#define GREEDY_ITERATIONS 20000

struct handoff {
    struct mutex mutex;
    u32 greedy_count;
    struct completion done;
};

static void hammer(void *user)
{
    struct handoff *h = user;
    u32 i;

    for (i = 0; i < GREEDY_ITERATIONS; i++) {
        mutex_lock(&h->mutex);
        atomic_store_relaxed(&h->greedy_count, i + 1);
        yield();
        mutex_unlock(&h->mutex);
    }

    complete(&h->done);
}

KTEST_CASE(waiter_is_not_starved_by_a_hammering_holder)
{
    struct handoff h = { .done = COMPLETION_INIT(h.done) };
    u32 before, after;

    mutex_init(&h.mutex);

    KTEST_ASSERT(!is_error(kthread_run(hammer, &h, "hammer")));
    sleep_ns(5 * NS_PER_MS);

    before = atomic_load_relaxed(&h.greedy_count);
    mutex_lock(&h.mutex);
    after = atomic_load_relaxed(&h.greedy_count);
    mutex_unlock(&h.mutex);

    wait_for_completion(&h.done);

    KTEST_EXPECT_GE(after, 1);
    KTEST_EXPECT_LE(after - before, 2);
}

struct holder {
    struct mutex mutex;
    struct completion locked;
    struct completion release;
    struct completion done;
};

static void hold_until_released(void *user)
{
    struct holder *h = user;

    mutex_lock(&h->mutex);
    complete(&h->locked);

    wait_for_completion(&h->release);
    mutex_unlock(&h->mutex);

    complete(&h->done);
}

KTEST_CASE(try_lock_fails_while_another_thread_holds_it)
{
    struct holder h = {
        .locked = COMPLETION_INIT(h.locked),
        .release = COMPLETION_INIT(h.release),
        .done = COMPLETION_INIT(h.done),
    };

    mutex_init(&h.mutex);

    KTEST_ASSERT(!is_error(kthread_run(hold_until_released, &h, "holder")));
    wait_for_completion(&h.locked);

    KTEST_EXPECT(!mutex_try_lock(&h.mutex));

    complete(&h.release);
    wait_for_completion(&h.done);

    KTEST_EXPECT(mutex_try_lock(&h.mutex));
    mutex_unlock(&h.mutex);
}
