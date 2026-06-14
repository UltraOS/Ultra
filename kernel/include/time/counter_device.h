#pragma once

#include <common/types.h>
#include <common/list.h>
#include <common/bit.h>
#include <common/error.h>

#include <time/units.h>

enum counter_device_rating : u8 {
    /*
     * 0..7 -> A really bad and slow time counter. There isn't a real device
     *         to use an example here, but a timer may lower it's rating to
     *         this tier on purpose if it detects defects or really expensive
     *         quirks/workarounds that are required
     */
    COUNTER_DEVICE_RATING_BAD = 0,

    /*
     * 8..15 -> A time counter that barely works for real use, but still usable
     *          in case we lack anything better. E.g. the i8253, which requires
     *          software emulation of a freerunning counter
     */
    COUNTER_DEVICE_RATING_OK = 8,

    /*
     * 16..31 -> A good time counter. E.g. the ACPI PM timer that works fine
     *           but overflows very often due to the 24-bit counter
     */
    COUNTER_DEVICE_RATING_GOOD = 16,

    /*
     * 32..63 -> A great time counter. HPET is a good example since it has
     *           a large counter, but it is still an external device that may
     *           be very slow to read in case of bus contention
     */
    COUNTER_DEVICE_RATING_GREAT = 32,

    /*
     * 64..255 -> An ideal time counter, e.g. invariant TSC. Very fast to read
     *            and is not affected by system load. Paravirtualized timers
     *            also mostly go here
     */
    COUNTER_DEVICE_RATING_IDEAL = 64,
};

#define COUNTER_MASK(bits) MAKE_BIT_MASK_U64((bits) - 1, 0)

struct counter_device {
    // Name of this device for pretty-printing
    const char *name;

    // A mandatory callback that returns the current counter value
    u64 (*read)(struct counter_device*);

    /*
     * The mask to apply to the value read from the counter
     * E.g. if your timer has a 24-bit counter:
     *     .mask = COUNTER_MASK(24)
     */
    u64 mask;

    /*
     * The rating of this time counter device.
     * See comments above the enum values to decide how to pick the correct
     * rating for your timer
     */
    enum counter_device_rating rating;

    /*
     * The maximum number of cycles this counter device can run for before
     * overflowing during the 'cycles * mult' math. The time keeper tries to
     * make sure the device is queried often enough that the fast u64 mult path
     * remains usable most of the time
     */
    u64 max_cycles_before_u64_overflow;

    /*
     * Number of nanoseconds this counter can run for before it starts
     * requiring attention from the time keeper (it will either overflow
     * shortly after, or degrade into slow path in cycles_to_ns)
     */
    u64 max_unattended_ns;

    /*
     * A safeguard value we use to detect when we have potentially missed
     * a counter overflow, or the counter is misbehaving and going backwards.
     * This is typically set to some high percentage of the maximum possible
     * hardware cycles value
     */
    u64 max_acceptable_cycles_delta;

    /*
     * Multiplier and shift values that are used to convert this counter
     * value into nanoseconds
     */
    u32 mult, shift;

    // Internal time counter device list link
    struct list_link link;
};

/*
 * Register a counter device that ticks at the rate of 'hz'
 *
 * No fields other than 'name', 'read', 'mask', and 'rating' are expected
 * to be populated here
 */
error_t counter_device_register(struct counter_device *dev, u64 hz);

/*
 * Unregister a counter device
 *
 * Note that if this is called after INIT_LEVEL_SMP_ONLINE, the device is
 * expected to remain online and premanently in memory as other CPUs might
 * be preempted after acquiring a reference to the counter device but before
 * reading it. No explicit synchronization is performed by the time keeper
 * to keep the hot path lockless
 */
void counter_device_unregister(struct counter_device *dev);

/*
 * Returns the current 'reference' counter device
 * This is typically the best counter device we have registered so far
 */
struct counter_device *current_counter_device(void);
