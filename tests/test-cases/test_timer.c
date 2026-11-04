#include <common/types.h>

u64 timer_test_now(void);

#define ns_since_boot timer_test_now

#include <kernel-source/time/timer.c>
#include <kernel-source/time/event_device.c>
#include <private/arch/preempt.h>
#include <arch/registers.h>

#include <test_harness.h>

#define NOT_ARMED UINT64_MAX
#define MAX_FIRED 16

static u64 s_now;
static u64 s_armed_for;
static int s_num_arms;
static int s_num_disarms;

static struct timer *s_fired[MAX_FIRED];
static int s_num_fired;

u64 timer_test_now(void)
{
    return s_now;
}

static void fake_arm(struct event_device *dev, u64 ticks)
{
    UNREFERENCED_PARAMETER(dev);

    s_armed_for = s_now + ticks;
    s_num_arms++;
}

static void fake_disarm(struct event_device *dev)
{
    UNREFERENCED_PARAMETER(dev);

    s_armed_for = NOT_ARMED;
    s_num_disarms++;
}

static struct event_device s_fake_device = {
    .name = "fake",
    .arm = fake_arm,
    .disarm = fake_disarm,
    .max_ticks = UINT64_MAX,
};

static void reset_state(void)
{
    memset(&s_queue, 0, sizeof(s_queue));

    s_now = 0;
    s_armed_for = NOT_ARMED;
    s_num_arms = 0;
    s_num_disarms = 0;
    s_num_fired = 0;

    event_device_register(&s_fake_device, NS_PER_SEC);
}

static void take_interrupt_at(u64 now)
{
    struct registers regs = { .irq_state = false };
    irq_state_t irq_state;

    s_now = now;
    s_armed_for = NOT_ARMED;

    irq_state = local_irq_state_save();
    hard_irq_enter();
    event_device_interrupt(&s_fake_device);
    hard_irq_exit(&regs);
    local_irq_state_restore(irq_state);
}

static enum timer_result record(struct timer *timer)
{
    if (s_num_fired < MAX_FIRED)
        s_fired[s_num_fired++] = timer;

    return TIMER_RESULT_DONE;
}

TEST_CASE(timer_empty_queue_disarms_the_device)
{
    reset_state();

    ASSERT_EQ(s_armed_for, NOT_ARMED);
    ASSERT_EQ(s_num_disarms, 1);
    ASSERT_EQ(s_num_arms, 0);
}

TEST_CASE(timer_fires_in_deadline_order)
{
    struct timer timers[3];
    int i;

    reset_state();

    for (i = 0; i < 3; i++)
        timer_init(&timers[i], record);

    timer_arm(&timers[0], 300);
    timer_arm(&timers[1], 100);
    timer_arm(&timers[2], 200);

    take_interrupt_at(1000);

    ASSERT_EQ(s_num_fired, 3);
    ASSERT_EQ(s_fired[0], &timers[1]);
    ASSERT_EQ(s_fired[1], &timers[2]);
    ASSERT_EQ(s_fired[2], &timers[0]);
    ASSERT_EQ(s_armed_for, NOT_ARMED);
}

TEST_CASE(timer_does_not_fire_early)
{
    struct timer early, late;

    reset_state();

    timer_init(&early, record);
    timer_init(&late, record);
    timer_arm(&early, 100);
    timer_arm(&late, 200);

    take_interrupt_at(99);
    ASSERT_EQ(s_num_fired, 0);
    ASSERT_EQ(s_armed_for, 100);

    take_interrupt_at(100);
    ASSERT_EQ(s_num_fired, 1);
    ASSERT_EQ(s_fired[0], &early);
    ASSERT_EQ(s_armed_for, 200);

    take_interrupt_at(250);
    ASSERT_EQ(s_num_fired, 2);
    ASSERT_EQ(s_armed_for, NOT_ARMED);
}

TEST_CASE(timer_arms_the_device_for_a_new_earliest_only)
{
    struct timer first, earlier, later;

    reset_state();

    timer_init(&first, record);
    timer_init(&earlier, record);
    timer_init(&later, record);

    timer_arm(&first, 500);
    ASSERT_EQ(s_armed_for, 500);
    ASSERT_EQ(s_num_arms, 1);

    timer_arm(&later, 900);
    ASSERT_EQ(s_armed_for, 500);
    ASSERT_EQ(s_num_arms, 1);

    timer_arm(&earlier, 200);
    ASSERT_EQ(s_armed_for, 200);
    ASSERT_EQ(s_num_arms, 2);
}

TEST_CASE(timer_cancel_keeps_it_from_firing)
{
    struct timer first, second;

    reset_state();

    timer_init(&first, record);
    timer_init(&second, record);
    timer_arm(&first, 100);
    timer_arm(&second, 200);

    ASSERT(timer_cancel_nosync(&first));
    ASSERT_EQ(s_armed_for, 100);

    take_interrupt_at(150);
    ASSERT_EQ(s_num_fired, 0);
    ASSERT_EQ(s_armed_for, 200);

    timer_cancel(&second);
    take_interrupt_at(300);
    ASSERT_EQ(s_num_fired, 0);
    ASSERT_EQ(s_armed_for, NOT_ARMED);

    ASSERT(timer_cancel_nosync(&first));
}

TEST_CASE(timer_cancel_of_a_never_armed_timer)
{
    struct timer timer;

    reset_state();
    timer_init(&timer, record);

    ASSERT(timer_cancel_nosync(&timer));
    timer_cancel(&timer);
}

TEST_CASE(timer_rearm_moves_the_deadline)
{
    struct timer timer;

    reset_state();
    timer_init(&timer, record);

    timer_arm(&timer, 100);
    timer_arm(&timer, 400);
    ASSERT_EQ(s_armed_for, 400);

    take_interrupt_at(200);
    ASSERT_EQ(s_num_fired, 0);

    take_interrupt_at(400);
    ASSERT_EQ(s_num_fired, 1);
}

TEST_CASE(timer_rearm_to_the_same_deadline_leaves_the_device_alone)
{
    struct timer timer;

    reset_state();

    timer_init(&timer, record);
    timer_arm(&timer, 500);
    timer_arm(&timer, 500);
    ASSERT_EQ(s_armed_for, 500);
    ASSERT_EQ(s_num_arms, 1);
}

TEST_CASE(timer_early_interrupt_arms_the_device_again)
{
    struct timer timer;

    reset_state();

    timer_init(&timer, record);
    timer_arm(&timer, 500);
    ASSERT_EQ(s_num_arms, 1);

    take_interrupt_at(300);
    ASSERT_EQ(s_num_fired, 0);
    ASSERT_EQ(s_armed_for, 500);
    ASSERT_EQ(s_num_arms, 2);
}

TEST_CASE(timer_with_slack_rides_an_earlier_interrupt)
{
    struct timer exact, relaxed;

    reset_state();

    timer_init(&exact, record);
    timer_init(&relaxed, record);

    timer_arm(&exact, 100);
    timer_arm_with_slack(&relaxed, 90, 410);
    ASSERT_EQ(s_armed_for, 100);
    ASSERT_EQ(s_num_arms, 1);

    take_interrupt_at(100);

    ASSERT_EQ(s_num_fired, 2);
    ASSERT_EQ(s_fired[0], &exact);
    ASSERT_EQ(s_fired[1], &relaxed);
    ASSERT_EQ(s_armed_for, NOT_ARMED);
}

TEST_CASE(timer_with_slack_alone_waits_until_the_slack_runs_out)
{
    struct timer relaxed;

    reset_state();

    timer_init(&relaxed, record);
    timer_arm_with_slack(&relaxed, 90, 410);
    ASSERT_EQ(s_armed_for, 500);
    ASSERT_EQ(timer_deadline(&relaxed), 90);

    take_interrupt_at(80);
    ASSERT_EQ(s_num_fired, 0);
    ASSERT_EQ(s_armed_for, 500);
}

struct periodic {
    struct timer timer;
    u64 period_ns;
    u64 num_periods;
    int runs;
    bool cancel_result;
};

static enum timer_result rearm(struct timer *timer)
{
    struct periodic *periodic;

    periodic = container_of(timer, struct periodic, timer);
    periodic->runs++;

    timer_arm(timer, timer_deadline(timer) + periodic->period_ns);

    return TIMER_RESULT_DONE;
}

static enum timer_result cancel_self(struct timer *timer)
{
    struct periodic *periodic;

    periodic = container_of(timer, struct periodic, timer);
    periodic->runs++;
    periodic->cancel_result = timer_cancel_nosync(timer);

    return TIMER_RESULT_DONE;
}

static enum timer_result advance(struct timer *timer)
{
    struct periodic *periodic;

    periodic = container_of(timer, struct periodic, timer);
    periodic->runs++;
    periodic->num_periods = timer_advance(timer, periodic->period_ns);

    return TIMER_RESULT_REARM;
}

static enum timer_result arm_and_ask_to_rearm(struct timer *timer)
{
    timer_arm(timer, 500);
    return TIMER_RESULT_REARM;
}

TEST_CASE(timer_callback_can_rearm_itself)
{
    struct periodic periodic = { .period_ns = 100 };
    int arms_before;

    reset_state();

    timer_init(&periodic.timer, rearm);
    timer_arm(&periodic.timer, 100);

    arms_before = s_num_arms;
    take_interrupt_at(100);

    ASSERT_EQ(periodic.runs, 1);
    ASSERT_EQ(s_armed_for, 200);
    ASSERT_EQ(s_num_arms, arms_before + 1);

    take_interrupt_at(450);
    ASSERT_EQ(periodic.runs, 4);
    ASSERT_EQ(s_armed_for, 500);

    ASSERT(timer_cancel_nosync(&periodic.timer));
}

TEST_CASE(timer_cancel_from_its_own_callback_reports_it_running)
{
    struct periodic periodic = { .cancel_result = true };

    reset_state();

    timer_init(&periodic.timer, cancel_self);
    timer_arm(&periodic.timer, 100);

    take_interrupt_at(100);

    ASSERT_EQ(periodic.runs, 1);
    ASSERT(!periodic.cancel_result);
    ASSERT(timer_cancel_nosync(&periodic.timer));
}

TEST_CASE(timer_advance_rearms_for_the_next_period)
{
    struct periodic periodic = { .period_ns = 100 };
    int arms_before;

    reset_state();

    timer_init(&periodic.timer, advance);
    timer_arm(&periodic.timer, 100);

    arms_before = s_num_arms;
    take_interrupt_at(100);

    ASSERT_EQ(periodic.runs, 1);
    ASSERT_EQ(periodic.num_periods, 1);
    ASSERT_EQ(timer_deadline(&periodic.timer), 200);
    ASSERT_EQ(s_armed_for, 200);
    ASSERT_EQ(s_num_arms, arms_before + 1);

    ASSERT(timer_cancel_nosync(&periodic.timer));
}

TEST_CASE(timer_advance_skips_the_periods_it_missed)
{
    struct periodic periodic = { .period_ns = 100 };

    reset_state();

    timer_init(&periodic.timer, advance);
    timer_arm(&periodic.timer, 100);

    take_interrupt_at(450);

    ASSERT_EQ(periodic.runs, 1);
    ASSERT_EQ(periodic.num_periods, 4);
    ASSERT_EQ(timer_deadline(&periodic.timer), 500);
    ASSERT_EQ(s_armed_for, 500);

    ASSERT(timer_cancel_nosync(&periodic.timer));
}

TEST_CASE(timer_advance_keeps_the_slack)
{
    struct periodic periodic = { .period_ns = 100 };

    reset_state();

    timer_init(&periodic.timer, advance);
    timer_arm_with_slack(&periodic.timer, 100, 50);

    take_interrupt_at(150);

    ASSERT_EQ(periodic.num_periods, 1);
    ASSERT_EQ(timer_deadline(&periodic.timer), 200);
    ASSERT_EQ(s_armed_for, 250);

    ASSERT(timer_cancel_nosync(&periodic.timer));
}

TEST_CASE(timer_armed_while_its_callback_runs_ignores_the_rearm)
{
    struct timer timer;

    reset_state();

    timer_init(&timer, arm_and_ask_to_rearm);
    timer_arm(&timer, 100);

    take_interrupt_at(100);

    ASSERT_EQ(timer_deadline(&timer), 500);
    ASSERT_EQ(s_armed_for, 500);

    ASSERT(timer_cancel_nosync(&timer));
    ASSERT(queue_first(this_cpu_ptr(&s_queue)) == nullptr);
}

TEST_CASE(timer_deadline_in_the_past_is_armed_for_now)
{
    struct timer timer;

    reset_state();
    s_now = 1000;

    timer_init(&timer, record);
    timer_arm(&timer, 400);
    ASSERT_EQ(s_armed_for, 1000);

    take_interrupt_at(1000);
    ASSERT_EQ(s_num_fired, 1);
}

TEST_CASE(timer_registering_a_device_arms_what_is_queued)
{
    struct timer timer;

    reset_state();
    memset(&s_queue, 0, sizeof(s_queue));
    s_armed_for = NOT_ARMED;

    timer_init(&timer, record);
    timer_arm(&timer, 700);
    ASSERT_EQ(s_armed_for, NOT_ARMED);

    event_device_register(&s_fake_device, NS_PER_SEC);
    ASSERT_EQ(s_armed_for, 700);

    ASSERT(timer_cancel_nosync(&timer));
}

TEST_CASE(timer_far_deadline_is_armed_for_the_longest_the_device_allows)
{
    static struct event_device s_fast_device = {
        .name = "fast",
        .arm = fake_arm,
        .disarm = fake_disarm,
        .max_ticks = UINT32_MAX,
    };
    struct timer timer;

    reset_state();

    // 2^62 ns at 4 GHz is 2^64 ticks, one more than a u64 holds
    event_device_register(&s_fast_device, 4 * NS_PER_SEC);

    timer_init(&timer, record);
    timer_arm(&timer, 1ull << 62);
    ASSERT_EQ(s_armed_for, UINT32_MAX);

    ASSERT(timer_cancel_nosync(&timer));
}

#define NUM_SIMULTANEOUS_TIMERS 1000

static int s_num_counted;

static enum timer_result count(struct timer *timer)
{
    UNREFERENCED_PARAMETER(timer);
    s_num_counted++;

    return TIMER_RESULT_DONE;
}

TEST_CASE(timer_interrupt_runs_everything_that_is_due)
{
    static struct timer s_timers[NUM_SIMULTANEOUS_TIMERS];
    int i;

    reset_state();
    s_num_counted = 0;

    for (i = 0; i < NUM_SIMULTANEOUS_TIMERS; i++) {
        timer_init(&s_timers[i], count);
        timer_arm(&s_timers[i], 100);
    }

    take_interrupt_at(100);

    ASSERT_EQ(s_num_counted, NUM_SIMULTANEOUS_TIMERS);
    ASSERT_EQ(s_armed_for, NOT_ARMED);
}

static struct timer *s_moving_timer;
static struct timer_queue *s_moving_to;
static int s_num_relaxes;

// Plays the CPU that finishes moving s_moving_timer to s_moving_to
static void finish_the_move(void)
{
    atomic_store_release(&s_moving_timer->queue, s_moving_to);
    s_num_relaxes++;
    g_cpu_relax_hook = nullptr;
}

static void start_a_move(struct timer *timer, struct timer_queue *to)
{
    timer->queue = &s_moving_queue;
    s_moving_timer = timer;
    s_moving_to = to;
    s_num_relaxes = 0;
    g_cpu_relax_hook = finish_the_move;
}

TEST_CASE(timer_armed_on_another_queue_moves_to_this_one)
{
    struct timer_queue other = {};
    struct timer timer;

    reset_state();
    timer_init(&timer, record);

    timer.queue = &other;
    rb_node_insert_cached(&timer.node, &other.timers, timer_expires_before);
    timer.flags = TIMER_FLAG_QUEUED;

    timer_arm(&timer, 500);

    ASSERT(timer.queue == this_cpu_ptr(&s_queue));
    ASSERT(queue_first(&other) == nullptr);
    ASSERT(queue_first(this_cpu_ptr(&s_queue)) == &timer);
    ASSERT_EQ(s_armed_for, 500);

    ASSERT(timer_cancel_nosync(&timer));
}

TEST_CASE(timer_arm_waits_for_a_timer_moving_between_queues)
{
    struct timer_queue other = {};
    struct timer timer;

    reset_state();
    timer_init(&timer, record);
    start_a_move(&timer, &other);

    timer_arm(&timer, 500);

    ASSERT_EQ(s_num_relaxes, 1);
    ASSERT(timer.queue == this_cpu_ptr(&s_queue));
    ASSERT(queue_first(this_cpu_ptr(&s_queue)) == &timer);
    ASSERT(queue_first(&other) == nullptr);

    ASSERT(timer_cancel_nosync(&timer));
}

TEST_CASE(timer_cancel_waits_for_a_timer_moving_between_queues)
{
    struct timer timer;

    reset_state();
    timer_init(&timer, record);
    timer_arm(&timer, 500);
    start_a_move(&timer, this_cpu_ptr(&s_queue));

    ASSERT(timer_cancel_nosync(&timer));

    ASSERT_EQ(s_num_relaxes, 1);
    ASSERT(queue_first(this_cpu_ptr(&s_queue)) == nullptr);
}

TEST_CASE(timer_rearmed_while_running_elsewhere_stays_there)
{
    struct timer_queue other = { .in_interrupt = true };
    struct timer timer;
    int arms_before;

    reset_state();
    timer_init(&timer, record);

    timer.queue = &other;
    timer.flags = TIMER_FLAG_RUNNING;

    arms_before = s_num_arms;
    timer_arm(&timer, 500);

    ASSERT(timer.queue == &other);
    ASSERT(queue_first(&other) == &timer);
    ASSERT(queue_first(this_cpu_ptr(&s_queue)) == nullptr);
    ASSERT_EQ(s_num_arms, arms_before);
}
