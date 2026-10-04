#include <common/types.h>

struct timer;

void test_timer_arm_with_slack(
    struct timer *timer, u64 deadline_ns, u64 slack_ns
);
bool test_timer_cancel_nosync(struct timer *timer);
u64 test_timer_advance(struct timer *timer, u64 period_ns);

#define timer_arm_with_slack test_timer_arm_with_slack
#define timer_cancel_nosync test_timer_cancel_nosync
#define timer_advance test_timer_advance

#include <kernel-source/time/keeper.c>
#undef MSG_FMT
#include <kernel-source/time/counter_device.c>
#undef MSG_FMT

#include <test_harness.h>

#include <common/bit.h>
#include <time/units.h>

/*
 * A single software counter shared by the test devices. Tests drive time
 * forward by writing to this and then reading it back through a registered
 * counter device.
 */
static u64 g_counter_value;

static u64 test_counter_read(struct counter_device *dev)
{
    (void)dev;
    return g_counter_value;
}

#define TEST_CD(n, m, r)                     \
    (struct counter_device) {                \
        .name = n,                           \
        .read = test_counter_read,           \
        .mask = m,                           \
        .rating = COUNTER_DEVICE_RATING_##r, \
    }

static u64 s_tick_deadline_ns;
static u64 s_tick_slack_ns;
static u64 s_tick_period_ns;
static bool s_tick_armed;

void test_timer_arm_with_slack(
    struct timer *timer, u64 deadline_ns, u64 slack_ns
)
{
    UNREFERENCED_PARAMETER(timer);

    s_tick_deadline_ns = deadline_ns;
    s_tick_slack_ns = slack_ns;
    s_tick_armed = true;
}

bool test_timer_cancel_nosync(struct timer *timer)
{
    UNREFERENCED_PARAMETER(timer);

    s_tick_armed = false;
    return true;
}

u64 test_timer_advance(struct timer *timer, u64 period_ns)
{
    UNREFERENCED_PARAMETER(timer);

    s_tick_period_ns = period_ns;
    return 1;
}

static void time_test_reset(void)
{
    list_init(&s_counter_devices);
    spin_lock_init(&s_counter_lock);
    s_active_cd = nullptr;

    spin_lock_init(&s_ctx.lock);
    seqcount_latch_init(&s_ctx.time_seq);
    s_ctx.cd = nullptr;
    s_ctx.ns = 0;
    s_ctx.ns_frac = 0;
    s_ctx.prev_cycles = 0;
    memset(s_ctx.published, 0, sizeof(s_ctx.published));
    s_test_init_level = INIT_LEVEL_PLATFORM_INFO_AVAILABLE;
    s_tick_armed = false;

    g_counter_value = 0;
}

// The exact (128-bit) conversion the fast path must always agree with.
static u64 reference_cycles_to_ns(struct counter_device *dev, u64 cycles)
{
    return (u64)(((u128)cycles * dev->mult) >> dev->shift);
}

static u64 abs_diff(u64 a, u64 b)
{
    return a > b ? a - b : b - a;
}

static void check_frequency(u64 hz)
{
    struct counter_device dev = TEST_CD("test", COUNTER_MASK(64), GOOD);
    u64 max_safe_mult, one_second_ns, tolerance;

    time_test_reset();
    counter_device_register(&dev, hz);

    // The shift starts at its ceiling and only ever drops from there.
    ASSERT(dev.shift <= 32);
    ASSERT(dev.mult != 0);

    /*
     * The whole point of the shift search is to keep the multiplier small
     * enough that the design-target unattended window never overflows the
     * fast u64 multiply.
     */
    max_safe_mult = UNSIGNED_MAX(u64) / (hz * IDEAL_UNATTENDED_SECONDS);
    ASSERT(dev.mult <= max_safe_mult);
    ASSERT(
        (hz * IDEAL_UNATTENDED_SECONDS) <= dev.max_cycles_before_u64_overflow
    );

    /*
     * One second's worth of cycles must convert back to one second within the
     * rounding error of the chosen mult/shift.
     */
    one_second_ns = counter_device_cycles_to_ns(&dev, hz);
    tolerance = (hz >> dev.shift) + 2;
    ASSERT(abs_diff(one_second_ns, NS_PER_SEC) <= tolerance);
}

TEST_CASE(counter_mult_shift_accuracy)
{
    check_frequency(100); // super slow
    check_frequency(1000);
    check_frequency(32768); // RTC crystal
    check_frequency(1193182); // i8253 PIT
    check_frequency(3579545); // ACPI PM timer
    check_frequency(14318180); // HPET-ish
    check_frequency(100 * MHZ);
    check_frequency(1 * GHZ);
    check_frequency(3 * GHZ);
    check_frequency(100ull * GHZ); // absurdly fast
}

// The derived thresholds must match their defining formulas exactly
TEST_CASE(counter_thresholds_match_formula)
{
    u64 mask = COUNTER_MASK(24);
    u64 limit, expected_unattended;
    struct counter_device dev = TEST_CD("pm", mask, GOOD);

    time_test_reset();
    counter_device_register(&dev, 3579545);

    ASSERT_EQ(dev.max_cycles_before_u64_overflow, UNSIGNED_MAX(u64) / dev.mult);
    ASSERT_EQ(dev.max_acceptable_cycles_delta, mask - (mask >> 3));

    limit = MIN(dev.max_cycles_before_u64_overflow, mask) >> 1;
    expected_unattended = (u64)(((u128)limit * dev.mult) >> dev.shift);
    ASSERT_EQ(dev.max_unattended_ns, expected_unattended);
}

/*
 * cycles_to_ns has a fast u64 path and a 128-bit slow path that kicks in past
 * max_cycles_before_u64_overflow. Both must produce the exact same value as
 * the reference 128-bit computation, especially right across the boundary.
 */
TEST_CASE(counter_cycles_to_ns_matches_reference)
{
    struct counter_device dev = TEST_CD("tsc", COUNTER_MASK(64), IDEAL);
    u64 boundary, samples[8];
    size_t i;

    time_test_reset();
    counter_device_register(&dev, 3 * GHZ);

    boundary = dev.max_cycles_before_u64_overflow;

    // Just below the boundary the fast path must not overflow
    ASSERT((boundary - 1) <= UNSIGNED_MAX(u64) / dev.mult);

    samples[0] = 0;
    samples[1] = 1;
    samples[2] = 3 * GHZ;
    samples[3] = boundary - 1;
    samples[4] = boundary;
    samples[5] = boundary + 1;
    samples[6] = boundary * 2;
    samples[7] = UNSIGNED_MAX(u64);

    for (i = 0; i < ARRAY_SIZE(samples); i++) {
        ASSERT_EQ(
            counter_device_cycles_to_ns(&dev, samples[i]),
            reference_cycles_to_ns(&dev, samples[i])
        );
    }
}

TEST_CASE(counter_delta_handles_wrap_and_glitches)
{
    u64 mask = COUNTER_MASK(24);
    u64 glitch;
    struct counter_device dev = TEST_CD("pm", mask, GOOD);

    time_test_reset();
    counter_device_register(&dev, 3579545);

    // Plain forward progress
    ASSERT_EQ(counter_device_delta(&dev, 0x100, 0x500), 0x400);

    // Wrapping across the 24-bit boundary yields the small real delta
    ASSERT_EQ(counter_device_delta(&dev, 0xFFFFF0, 0x000010), 0x20);

    // A delta past the acceptance threshold is treated as a glitch -> 0
    glitch = dev.max_acceptable_cycles_delta + 1;
    ASSERT_EQ(counter_device_delta(&dev, 0, glitch), 0);

    // A small backwards step masks to a near-full counter, also rejected
    ASSERT_EQ(counter_device_delta(&dev, 0x10, 0x0F), 0);

    // A zero delta is valid and stays zero
    ASSERT_EQ(counter_device_delta(&dev, 0x1234, 0x1234), 0);
}

TEST_CASE(counter_read_masks_value)
{
    u64 mask = COUNTER_MASK(24);
    struct counter_device dev = TEST_CD("pm", mask, GOOD);

    time_test_reset();
    counter_device_register(&dev, 3579545);

    g_counter_value = 0xABCDEF12345;
    ASSERT_EQ(counter_device_read(&dev), 0xABCDEF12345 & mask);

    g_counter_value = mask;
    ASSERT_EQ(counter_device_read(&dev), mask);
}

TEST_CASE(counter_register_rejects_invalid)
{
    struct counter_device dev = TEST_CD("bad", COUNTER_MASK(32), GOOD);

    time_test_reset();

    ASSERT_EQ(counter_device_register(&dev, 0), EINVAL);

    dev.mask = 0;
    ASSERT_EQ(counter_device_register(&dev, 1 * MHZ), EINVAL);

    // The wraparound math cannot work with a non-contiguous mask
    dev.mask = 0xF0F0;
    ASSERT_EQ(counter_device_register(&dev, 1 * MHZ), EINVAL);

    dev.mask = COUNTER_MASK(32);
    dev.read = nullptr;
    ASSERT_EQ(counter_device_register(&dev, 1 * MHZ), EINVAL);
}

TEST_CASE(counter_register_rejects_double)
{
    struct counter_device dev = TEST_CD("dup", COUNTER_MASK(32), GOOD);

    time_test_reset();

    ASSERT_EQ(counter_device_register(&dev, 1 * MHZ), EOK);
    ASSERT_EQ(counter_device_register(&dev, 1 * MHZ), EBUSY);

    // Unregistering makes the device usable again
    counter_device_unregister(&dev);
    ASSERT_EQ(counter_device_register(&dev, 1 * MHZ), EOK);
}

TEST_CASE(counter_register_best_selection)
{
    struct counter_device ok = TEST_CD("ok", COUNTER_MASK(32), OK);
    struct counter_device great = TEST_CD("great", COUNTER_MASK(64), GREAT);
    struct counter_device good = TEST_CD("good", COUNTER_MASK(24), GOOD);

    time_test_reset();

    ASSERT_EQ((uintptr_t)current_counter_device(), 0);

    counter_device_register(&ok, 1 * MHZ);
    ASSERT_EQ((uintptr_t)current_counter_device(), (uintptr_t)&ok);

    // A better device takes over as the reference
    counter_device_register(&great, 1 * GHZ);
    ASSERT_EQ((uintptr_t)current_counter_device(), (uintptr_t)&great);
    ASSERT_EQ((uintptr_t)s_ctx.cd, (uintptr_t)&great);

    // A worse device is kept around but does not become the reference
    counter_device_register(&good, 3579545);
    ASSERT_EQ((uintptr_t)current_counter_device(), (uintptr_t)&great);
}

#define ONE_NS_PER_CYCLE_HZ (1 * GHZ)

TEST_CASE(keeper_without_device_is_zero)
{
    time_test_reset();

    ASSERT_EQ(ns_since_boot(), 0);

    // Ticking with no counter device installed is a harmless no-op.
    tick_timer_fn(&s_ctx.tick_timer);
    ASSERT_EQ(ns_since_boot(), 0);
}

TEST_CASE(ns_from_now_saturates)
{
    struct counter_device dev = TEST_CD("tsc", COUNTER_MASK(64), IDEAL);

    time_test_reset();
    counter_device_register(&dev, ONE_NS_PER_CYCLE_HZ);

    g_counter_value = 1000;
    ASSERT_EQ(ns_from_now(500), 1500);
    ASSERT_EQ(ns_from_now(UNSIGNED_MAX(u64)), NO_DEADLINE);

    counter_device_unregister(&dev);
}

TEST_CASE(keeper_accumulates_across_ticks)
{
    struct counter_device dev = TEST_CD("tsc", COUNTER_MASK(64), IDEAL);

    time_test_reset();
    counter_device_register(&dev, ONE_NS_PER_CYCLE_HZ);

    g_counter_value = 1000;
    tick_timer_fn(&s_ctx.tick_timer);
    ASSERT_EQ(ns_since_boot(), 1000);

    g_counter_value = 5000;
    tick_timer_fn(&s_ctx.tick_timer);
    ASSERT_EQ(ns_since_boot(), 5000);
}

// Between ticks, ns_since_boot folds in the live delta off the raw counter
TEST_CASE(keeper_reports_live_delta)
{
    struct counter_device dev = TEST_CD("tsc", COUNTER_MASK(64), IDEAL);

    time_test_reset();
    counter_device_register(&dev, ONE_NS_PER_CYCLE_HZ);

    g_counter_value = 1000;
    tick_timer_fn(&s_ctx.tick_timer);

    // No tick here: the extra 500 cycles are observed live
    g_counter_value = 1500;
    ASSERT_EQ(ns_since_boot(), 1500);
}

TEST_CASE(keeper_carries_the_fraction_across_ticks)
{
    struct counter_device dev = TEST_CD("tsc", COUNTER_MASK(64), IDEAL);
    u64 i;

    time_test_reset();
    counter_device_register(&dev, 400 * MHZ);

    for (i = 1; i <= 1000; i++) {
        g_counter_value = i;
        tick_timer_fn(&s_ctx.tick_timer);
    }

    ASSERT_EQ(ns_since_boot(), reference_cycles_to_ns(&dev, 1000));
}

TEST_CASE(keeper_time_does_not_step_back_across_a_tick)
{
    struct counter_device dev = TEST_CD("tsc", COUNTER_MASK(64), IDEAL);
    u64 before;

    time_test_reset();
    counter_device_register(&dev, 400 * MHZ);

    g_counter_value = 2;
    before = ns_since_boot();

    // The tick read the counter before the reader did, but published after it
    g_counter_value = 1;
    tick_timer_fn(&s_ctx.tick_timer);

    g_counter_value = 2;
    ASSERT_EQ(ns_since_boot(), before);
}

/*
 * Swapping the active device must bank the elapsed time from the outgoing
 * device before continuing on the new one, so the timeline stays monotonic.
 */
TEST_CASE(keeper_banks_time_on_device_switch)
{
    struct counter_device a = TEST_CD("a", COUNTER_MASK(64), GOOD);
    struct counter_device b = TEST_CD("b", COUNTER_MASK(64), IDEAL);

    time_test_reset();
    counter_device_register(&a, ONE_NS_PER_CYCLE_HZ);

    g_counter_value = 2000;
    tick_timer_fn(&s_ctx.tick_timer);
    ASSERT_EQ(ns_since_boot(), 2000);

    // Switching to the better device banks a's outstanding 1000 cycles
    g_counter_value = 3000;
    counter_device_register(&b, ONE_NS_PER_CYCLE_HZ);
    ASSERT_EQ(ns_since_boot(), 3000);

    // From here on, b drives the clock
    g_counter_value = 3500;
    ASSERT_EQ(ns_since_boot(), 3500);
}

TEST_CASE(counter_unregister_repicks_best)
{
    struct counter_device good = TEST_CD("good", COUNTER_MASK(64), GOOD);
    struct counter_device great = TEST_CD("great", COUNTER_MASK(64), GREAT);

    time_test_reset();
    counter_device_register(&good, ONE_NS_PER_CYCLE_HZ);
    counter_device_register(&great, ONE_NS_PER_CYCLE_HZ);
    ASSERT_EQ((uintptr_t)current_counter_device(), (uintptr_t)&great);

    g_counter_value = 1000;
    tick_timer_fn(&s_ctx.tick_timer);

    // Dropping the active device falls back to the next best one
    counter_device_unregister(&great);
    ASSERT_EQ((uintptr_t)current_counter_device(), (uintptr_t)&good);
    ASSERT_EQ(ns_since_boot(), 1000);

    // Unregistering the last device freezes time at the banked value
    g_counter_value = 2000;
    counter_device_unregister(&good);
    ASSERT_EQ((uintptr_t)current_counter_device(), 0);
    ASSERT_EQ(ns_since_boot(), 2000);

    g_counter_value = 5000;
    ASSERT_EQ(ns_since_boot(), 2000);
}

// A second software counter for tests that need two independent devices
static u64 g_counter_b_value;

static u64 test_counter_b_read(struct counter_device *dev)
{
    (void)dev;
    return g_counter_b_value;
}

/*
 * Banking on a device switch must be independent of the new device's
 * absolute counter value, only deltas against its initial read may be
 * accumulated from that point on
 */
TEST_CASE(keeper_banking_ignores_new_counter_offset)
{
    struct counter_device a = TEST_CD("a", COUNTER_MASK(64), GOOD);
    struct counter_device b = TEST_CD("b", COUNTER_MASK(64), IDEAL);

    b.read = test_counter_b_read;

    time_test_reset();
    g_counter_b_value = 0xDEAD0000;
    counter_device_register(&a, ONE_NS_PER_CYCLE_HZ);

    g_counter_value = 2000;
    tick_timer_fn(&s_ctx.tick_timer);

    counter_device_register(&b, ONE_NS_PER_CYCLE_HZ);
    ASSERT_EQ(ns_since_boot(), 2000);

    g_counter_b_value += 500;
    ASSERT_EQ(ns_since_boot(), 2500);
}

/*
 * A counter that appears to jump backwards (or skips a wrap) produces a delta
 * past the acceptance threshold, which the keeper must discard rather than
 * letting time leap
 */
TEST_CASE(keeper_ignores_counter_glitch)
{
    u64 mask = COUNTER_MASK(24);
    u64 settled;
    struct counter_device dev = TEST_CD("pm", mask, GOOD);

    time_test_reset();
    counter_device_register(&dev, 3579545);

    g_counter_value = 0x1000;
    tick_timer_fn(&s_ctx.tick_timer);
    settled = ns_since_boot();

    // Counter jumps backwards, the resulting masked delta is rejected
    g_counter_value = 0x0FFF;
    tick_timer_fn(&s_ctx.tick_timer);
    ASSERT_EQ(ns_since_boot(), settled);
}

TEST_TEARDOWN()
{
    // ns_since_boot() is used by other tests as well, so make sure its clean
    time_test_reset();
}

static u64 g_seen_during_switch;
static bool g_switch_pending;

// The first read is the keeper sampling the device it is switching to
static u64 test_counter_read_during_switch(struct counter_device *dev)
{
    UNREFERENCED_PARAMETER(dev);

    if (g_switch_pending) {
        g_switch_pending = false;

        g_counter_value += 400;
        g_seen_during_switch = ns_since_boot();
    }

    return 0;
}

TEST_CASE(keeper_time_does_not_step_back_across_a_device_switch)
{
    struct counter_device old_dev = TEST_CD("old", COUNTER_MASK(64), GOOD);
    struct counter_device new_dev = {
        .name = "new",
        .read = test_counter_read_during_switch,
        .mask = COUNTER_MASK(64),
        .rating = COUNTER_DEVICE_RATING_GREAT,
    };

    time_test_reset();
    g_counter_value = 0;
    g_seen_during_switch = 0;
    g_switch_pending = true;

    ASSERT_EQ(counter_device_register(&old_dev, ONE_NS_PER_CYCLE_HZ), EOK);
    g_counter_value = 1000;

    ASSERT_EQ(counter_device_register(&new_dev, ONE_NS_PER_CYCLE_HZ), EOK);
    ASSERT_EQ((uintptr_t)s_ctx.cd, (uintptr_t)&new_dev);

    ASSERT_EQ(g_seen_during_switch, 1400);
    ASSERT(ns_since_boot() >= g_seen_during_switch);

    counter_device_unregister(&new_dev);
    counter_device_unregister(&old_dev);
}

TEST_CASE(keeper_read_does_not_wait_for_an_update_in_progress)
{
    struct counter_device dev = TEST_CD("test", COUNTER_MASK(64), GOOD);
    u64 before;

    time_test_reset();
    g_counter_value = 0;

    ASSERT_EQ(counter_device_register(&dev, ONE_NS_PER_CYCLE_HZ), EOK);
    g_counter_value = 1000;
    tick_timer_fn(&s_ctx.tick_timer);
    g_counter_value = 1500;

    before = ns_since_boot();
    ASSERT_EQ(before, 1500);

    seqcount_latch_flip(&s_ctx.time_seq);
    s_ctx.published[0].ns = 0xDEAD0000;
    s_ctx.published[0].prev_cycles = 7;
    ASSERT_EQ(ns_since_boot(), before);

    s_ctx.published[0] = s_ctx.published[1];
    seqcount_latch_flip(&s_ctx.time_seq);
    s_ctx.published[1].ns = 0xDEAD0000;
    ASSERT_EQ(ns_since_boot(), before);

    s_ctx.published[1] = s_ctx.published[0];
    counter_device_unregister(&dev);
}

TEST_CASE(keeper_tick_timer_stays_off_until_usable)
{
    struct counter_device dev = TEST_CD("pm", COUNTER_MASK(24), GOOD);

    time_test_reset();
    g_counter_value = 0;

    ASSERT_EQ(counter_device_register(&dev, ONE_NS_PER_CYCLE_HZ), EOK);
    ASSERT(!s_tick_armed);

    counter_device_unregister(&dev);
}

TEST_CASE(keeper_tick_timer_follows_the_counter)
{
    struct counter_device narrow = TEST_CD("pm", COUNTER_MASK(24), GOOD);
    struct counter_device wide = TEST_CD("hpet", COUNTER_MASK(32), GREAT);

    time_test_reset();
    g_counter_value = 0;

    s_test_init_level = INIT_LEVEL_PER_CPU_AVAILABLE;
    ASSERT_EQ(tick_timer_init(), EOK);
    ASSERT(!s_tick_armed);

    ASSERT_EQ(counter_device_register(&narrow, ONE_NS_PER_CYCLE_HZ), EOK);
    ASSERT(s_tick_armed);
    ASSERT_EQ(s_tick_deadline_ns, narrow.max_unattended_ns / 2);
    ASSERT_EQ(s_tick_slack_ns, narrow.max_unattended_ns / 2);

    g_counter_value = 1000;
    ASSERT_EQ(tick_timer_fn(&s_ctx.tick_timer), TIMER_RESULT_REARM);
    ASSERT_EQ(s_tick_period_ns, narrow.max_unattended_ns / 2);

    ASSERT_EQ(counter_device_register(&wide, ONE_NS_PER_CYCLE_HZ), EOK);
    ASSERT_EQ(s_tick_deadline_ns, 1000 + wide.max_unattended_ns / 2);
    ASSERT(wide.max_unattended_ns > narrow.max_unattended_ns);

    counter_device_unregister(&wide);
    ASSERT_EQ(s_tick_deadline_ns, 1000 + narrow.max_unattended_ns / 2);

    counter_device_unregister(&narrow);
    ASSERT(!s_tick_armed);
    ASSERT_EQ(tick_timer_fn(&s_ctx.tick_timer), TIMER_RESULT_DONE);
}
