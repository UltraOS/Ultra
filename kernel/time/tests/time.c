#include <ktest/ktest.h>

#include <common/atomic.h>

#include <sched/sched.h>
#include <sched/completion.h>
#include <time/time.h>
#include <time/timer.h>
#include <time/units.h>

#define NUM_MASS_TIMERS 500

struct counted_timer {
    struct timer timer;
    u32 num_fired;
};

static u32 s_num_mass_fired;
static struct completion s_all_mass_fired;

static enum timer_result count_mass_firing(struct timer *timer)
{
    struct counted_timer *counted;

    counted = container_of(timer, struct counted_timer, timer);
    counted->num_fired++;

    if (atomic_add_fetch(&s_num_mass_fired, 1, MO_RELAXED) == NUM_MASS_TIMERS)
        complete(&s_all_mass_fired);

    return TIMER_RESULT_DONE;
}

KTEST_CASE(timers_due_together_all_fire_once)
{
    static struct counted_timer s_timers[NUM_MASS_TIMERS];
    u64 deadline_ns;
    size_t i;

    atomic_store_relaxed(&s_num_mass_fired, 0);
    completion_init(&s_all_mass_fired);
    deadline_ns = ns_from_now(20 * NS_PER_MS);

    for (i = 0; i < NUM_MASS_TIMERS; i++) {
        s_timers[i].num_fired = 0;
        timer_init(&s_timers[i].timer, count_mass_firing);
        timer_arm(&s_timers[i].timer, deadline_ns);
    }

    wait_for_completion(&s_all_mass_fired);

    for (i = 0; i < NUM_MASS_TIMERS; i++)
        KTEST_EXPECT_EQ(s_timers[i].num_fired, 1);
}

static u32 s_num_fired;

static enum timer_result count_firing(struct timer *timer)
{
    UNREFERENCED_PARAMETER(timer);
    atomic_fetch_add(&s_num_fired, 1, MO_RELAXED);

    return TIMER_RESULT_DONE;
}

KTEST_CASE(cancelled_timer_does_not_fire)
{
    DEFINE_TIMER(queued, count_firing);
    DEFINE_TIMER(never_armed, count_firing);

    atomic_store_relaxed(&s_num_fired, 0);

    timer_arm(&queued, ns_from_now(10 * NS_PER_MS));
    timer_cancel(&queued);
    KTEST_EXPECT(timer_cancel_nosync(&never_armed));

    sleep_ns(30 * NS_PER_MS);

    KTEST_EXPECT_EQ(atomic_load_relaxed(&s_num_fired), 0);
}

struct periodic {
    struct timer timer;
    u32 runs;
    u32 wanted;
    u64 period_ns;
    struct completion done;
};

static enum timer_result rearm(struct timer *timer)
{
    struct periodic *p;

    p = container_of(timer, struct periodic, timer);

    if (++p->runs == p->wanted) {
        complete(&p->done);
        return TIMER_RESULT_DONE;
    }

    timer_advance(timer, p->period_ns);
    return TIMER_RESULT_REARM;
}

KTEST_CASE(timer_may_rearm_itself_from_its_callback)
{
    struct periodic p = {
        .wanted = 10,
        .period_ns = 2 * NS_PER_MS,
        .done = COMPLETION_INIT(p.done),
    };
    u64 before, after;

    timer_init(&p.timer, rearm);

    before = ns_since_boot();
    timer_arm(&p.timer, before + p.period_ns);
    wait_for_completion(&p.done);
    after = ns_since_boot();

    timer_cancel(&p.timer);

    KTEST_EXPECT_EQ(p.runs, p.wanted);
    KTEST_EXPECT_GE(after - before, p.wanted * p.period_ns);
}

#define NUM_CLOCK_READERS 2
#define NUM_CLOCK_READS 200000

struct clock_readers {
    u64 latest;
    u32 steps_back;
};

static void publish_latest(u64 *latest, u64 now)
{
    u64 old;

    old = atomic_load_relaxed(latest);

    while (old < now) {
        if (atomic_cmpxchg_acq_rel(latest, &old, now))
            break;
    }
}

static void read_the_clock(void *user, u32 index)
{
    struct clock_readers *readers = user;
    u64 seen, now;
    u32 i;

    UNREFERENCED_PARAMETER(index);

    for (i = 0; i < NUM_CLOCK_READS; i++) {
        /*
         * The latest time that any reader has seen so far. The time we read
         * next must not be earlier than that, no matter which reader published
         * it.
         */
        seen = atomic_load_acquire(&readers->latest);
        now = ns_since_boot();

        if (unlikely(now < seen))
            atomic_fetch_add(&readers->steps_back, 1, MO_RELAXED);

        publish_latest(&readers->latest, now);
    }
}

KTEST_CASE(clock_never_steps_back)
{
    struct clock_readers readers = { 0 };

    KTEST_RUN_THREADS(
        read_the_clock, &readers, NUM_CLOCK_READERS, "clock-reader"
    );
    KTEST_EXPECT_EQ(readers.steps_back, 0);
}
