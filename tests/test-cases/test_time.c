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

static void time_test_reset(void)
{
    list_init(&s_counter_devices);
    spin_lock_init(&s_counter_lock);
    s_active_cd = nullptr;

    spin_lock_init(&s_ctx.lock);
    seqcount_init(&s_ctx.time_seq);
    s_ctx.cd = nullptr;
    s_ctx.ns = 0;
    s_ctx.prev_cycles = 0;

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
    time_keeper_tick();
    ASSERT_EQ(ns_since_boot(), 0);
}

TEST_CASE(keeper_accumulates_across_ticks)
{
    struct counter_device dev = TEST_CD("tsc", COUNTER_MASK(64), IDEAL);

    time_test_reset();
    counter_device_register(&dev, ONE_NS_PER_CYCLE_HZ);

    g_counter_value = 1000;
    time_keeper_tick();
    ASSERT_EQ(ns_since_boot(), 1000);

    g_counter_value = 5000;
    time_keeper_tick();
    ASSERT_EQ(ns_since_boot(), 5000);
}

// Between ticks, ns_since_boot folds in the live delta off the raw counter
TEST_CASE(keeper_reports_live_delta)
{
    struct counter_device dev = TEST_CD("tsc", COUNTER_MASK(64), IDEAL);

    time_test_reset();
    counter_device_register(&dev, ONE_NS_PER_CYCLE_HZ);

    g_counter_value = 1000;
    time_keeper_tick();

    // No tick here: the extra 500 cycles are observed live
    g_counter_value = 1500;
    ASSERT_EQ(ns_since_boot(), 1500);
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
    time_keeper_tick();
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
    time_keeper_tick();

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
    time_keeper_tick();

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
    time_keeper_tick();
    settled = ns_since_boot();

    // Counter jumps backwards, the resulting masked delta is rejected
    g_counter_value = 0x0FFF;
    time_keeper_tick();
    ASSERT_EQ(ns_since_boot(), settled);
}

TEST_TEARDOWN()
{
    // ns_since_boot() is used by other tests as well, so make sure its clean
    time_test_reset();
}
