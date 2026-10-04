#pragma once

#include <common/types.h>
#include <common/error.h>

/*
 * A device that is able to generate one-shot interrupts at a point of time
 * in the future.
 */
struct event_device {
    // Name of this device for pretty-printing
    const char *name;

    /*
     * A mandatory callback that makes the device raise one interrupt 'ticks'
     * ticks from now, replacing a request if one was pending. The supplied
     * 'ticks' value is guaranteed to be within [min_ticks, max_ticks].
     */
    void (*arm)(struct event_device *dev, u64 ticks);

    // A mandatory callback that cancels a previous arm, if there is one
    void (*disarm)(struct event_device *dev);

    /*
     * The range that describes the number of ticks a device->arm() request is
     * able to accept and serve. The timer core clamps the requested deadline to
     * this range.
     */
    u64 min_ticks;
    u64 max_ticks;

    // Computed by event_device_register()
    u64 ns_to_ticks_mult;
};

// Make 'dev' ticking at 'hz' the event device of the calling CPU
error_t event_device_register(struct event_device *dev, u64 hz);

// The callback all event devices are expected to call on every interrupt
void event_device_interrupt(struct event_device *dev);
