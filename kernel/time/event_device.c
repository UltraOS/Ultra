#define MSG_FMT(msg) "event-device: " msg

#include <private/time/event_device.h>
#include <private/time/timer.h>

#include <time/event_device.h>
#include <time/time.h>
#include <time/units.h>

#include <common/minmax.h>
#include <common/overflow.h>

#include <bug.h>
#include <log.h>
#include <per_cpu.h>
#include <irq_helpers.h>

#define NS_TO_TICKS_SHIFT 32

static DEFINE_PER_CPU(struct event_device *, s_device);

static u64 ns_to_ticks_mult(u64 hz)
{
    u64 mult;

    mult = (hz / NS_PER_SEC) << NS_TO_TICKS_SHIFT;
    mult += ((hz % NS_PER_SEC) << NS_TO_TICKS_SHIFT) / NS_PER_SEC;

    return mult;
}

error_t event_device_register(struct event_device *dev, u64 hz)
{
    struct human_repr freq;
    irq_state_t irq_state;

    if (dev->arm == nullptr || dev->disarm == nullptr)
        return EINVAL;
    if (hz == 0 || dev->min_ticks > dev->max_ticks)
        return EINVAL;

    dev->ns_to_ticks_mult = ns_to_ticks_mult(hz);

    irq_state = local_irq_state_save();
    this_cpu_write(s_device, dev);
    timer_queue_set_event_device(dev);
    local_irq_state_restore(irq_state);

    frequency_to_human(hz, &freq);
    pr_info("%s @ " HUMAN_REPR_FMT "\n", dev->name, HUMAN_REPR_ARG(freq));
    return EOK;
}

void event_device_arm(struct event_device *dev, u64 deadline_ns)
{
    u64 delta_ns, ticks;

    delta_ns = saturating_sub(deadline_ns, ns_since_boot());

    /*
     * It's possible for a deadline to be much further away than what this
     * device can accept, but this is fine as we just clamp it to its maximum
     * here and then the timer core reprograms it again once it gets an
     * interrupt earlier and finds out there's nothing due.
     */
    ticks = CLAMP(
        ((u128)delta_ns * dev->ns_to_ticks_mult) >> NS_TO_TICKS_SHIFT,
        dev->min_ticks, dev->max_ticks
    );

    dev->arm(dev, ticks);
}

void event_device_interrupt(struct event_device *dev)
{
    BUG_ON(this_cpu_read(s_device) != dev);
    timer_queue_expire();
}
