#pragma once

#include <common/types.h>

#include <detail/time/timer.h>

/*
 * A callback run at a point in monotonic time, in hard interrupt
 * context on the CPU that armed the timer. Set up with TIMER_INIT() or
 * timer_init(), all fields are internal.
 */
struct timer;

// The result of a timer callback
enum timer_result : u32 {
    // Don't re-arm, this timer is done (or has re-armed itself already)
    TIMER_RESULT_DONE = 0,
};

typedef enum timer_result (*timer_fn_t)(struct timer *timer);

#define TIMER_INIT(name, timer_fn) (struct timer) { \
    .fn = (timer_fn),                               \
}
#define DEFINE_TIMER(name, timer_fn)               \
    struct timer name = TIMER_INIT(name, timer_fn)

void timer_init(struct timer *timer, timer_fn_t fn);

/*
 * Arm the timer to run its callback once at an absolute 'deadline_ns' in
 * ns_since_boot() time. If 'deadline_ns' is in the past, the callback is
 * invoked as soon as possible. To run it periodically, arm the timer again
 * from its callback, e.g. for timer_deadline() plus the period. Callable
 * from any context that may take a spinlock, including the callback itself.
 *
 * NOTE: The timer is armed on the calling CPU, unless its callback is running
 *       on another CPU at that moment, in which case it stays there. Arming a
 *       timer that is already armed changes its deadline.
 */
void timer_arm(struct timer *timer, u64 deadline_ns);

// The deadline the timer was last armed with
static inline u64 timer_deadline(const struct timer *timer)
{
    return timer->deadline_ns;
}

/*
 * Cancel a timer. The callback is guaranteed to not be running once this
 * returns.
 */
void timer_cancel(struct timer *timer);

/*
 * Cancel a timer. The callback may still be running once this function returns,
 * this is indicated by the return value.
 */
bool timer_cancel_nosync(struct timer *timer);
