#pragma once

#include <time/counter_device.h>

#include <common/bit.h>

/*
 * Convert cycles to nanoseconds, adding *in_out_frac, the fraction of a
 * nanosecond left over by a previous conversion with this device, and
 * storing the new one back. The fraction is in units of 2^-shift.
 */
static inline u64 counter_device_cycles_to_ns_frac(
    struct counter_device *dev, u64 cycles, u64 *in_out_frac
)
{
    u64 ns, frac, frac_mask;

    frac_mask = BIT_U64(dev->shift) - 1;

    if (likely(cycles < dev->max_cycles_before_u64_overflow)) {
        u64 shifted_ns;

        shifted_ns = cycles * dev->mult;
        ns = shifted_ns >> dev->shift;
        frac = shifted_ns & frac_mask;
    } else {
        u128 shifted_ns;

        shifted_ns = (u128)cycles * dev->mult;
        ns = (u64)(shifted_ns >> dev->shift);
        frac = (u64)shifted_ns & frac_mask;
    }

    frac += *in_out_frac;
    *in_out_frac = frac & frac_mask;

    return ns + (frac >> dev->shift);
}

static inline u64 counter_device_cycles_to_ns(
    struct counter_device *dev, u64 cycles
)
{
    u64 frac = 0;

    return counter_device_cycles_to_ns_frac(dev, cycles, &frac);
}

static inline u64 counter_device_delta(
    struct counter_device *cd, u64 prev_cycles, u64 cur_cycles
)
{
    u64 delta;

    delta = (cur_cycles - prev_cycles) & cd->mask;
    if (unlikely(delta > cd->max_acceptable_cycles_delta))
        /*
         * We must've missed a wraparound or this counter is buggy and jumped
         * back in time, discard this value
         */
        return 0;

    return delta;
}

static inline u64 counter_device_read(struct counter_device *dev)
{
    return dev->read(dev) & dev->mask;
}
