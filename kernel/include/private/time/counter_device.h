#pragma once

#include <time/counter_device.h>

static inline u64 counter_device_cycles_to_ns(
    struct counter_device *dev, u64 cycles
)
{
    if (likely(cycles < dev->max_cycles_before_u64_overflow))
        return (cycles * dev->mult) >> dev->shift;

    return (u64)(((u128)cycles * dev->mult) >> dev->shift);
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
