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

    /*
     * Re-arm the timer again. The callback is expected to have called
     * timer_advance() to set a new deadline before returning this. This is a
     * no-op if the timer was already re-armed earlier.
     */
    TIMER_RESULT_REARM,
};

typedef enum timer_result (*timer_fn_t)(struct timer *timer);

#define TIMER_INIT(name, timer_fn) (struct timer) { \
    .fn = (timer_fn),                               \
}
#define DEFINE_TIMER(name, timer_fn)               \
    struct timer name = TIMER_INIT(name, timer_fn)

void timer_init(struct timer *timer, timer_fn_t fn);

/*
 * Same as timer_arm(timer, deadline_ns + slack_ns), except the callback may
 * end up running earlier, but no earlier than 'deadline_ns'. This allows for
 * interrupt coalescing in case another timer's deadline happens to arrive in
 * that window. timer_deadline() reports the 'deadline_ns' value for such
 * timers.
 */
void timer_arm_with_slack(struct timer *timer, u64 deadline_ns, u64 slack_ns);

/*
 * Arm the timer to run its callback once at an absolute 'deadline_ns' in
 * ns_since_boot() time. If 'deadline_ns' is in the past, the callback is
 * invoked as soon as possible. Callable from any context that may take a
 * spinlock, including the callback itself.
 *
 * NOTE: The timer is armed on the calling CPU, unless its callback is running
 *       on another CPU at that moment, in which case it stays there. Arming a
 *       timer that is already armed changes its deadline.
 */
static inline void timer_arm(struct timer *timer, u64 deadline_ns)
{
    timer_arm_with_slack(timer, deadline_ns, 0);
}

/*
 * Like timer_arm(), for a timer that is re-armed to a later deadline far
 * more often than it fires. If the event device is already armed for an
 * earlier point, it is left alone, and the interrupt it raises then arms
 * the device for this timer.
 */
void timer_arm_lazy(struct timer *timer, u64 deadline_ns);

// The deadline the timer was last armed with
static inline u64 timer_deadline(const struct timer *timer)
{
    return timer->deadline_ns;
}

/*
 * Move the deadline of 'timer' forward by whole periods of 'period_ns', to
 * the first one after the current time, and return how many periods it
 * moved. The slack the timer was armed with is kept. Only callable from the
 * timer's own callback. Return TIMER_RESULT_REARM from the callback to re-arm
 * the timer at the new deadline.
 *
 * NOTE: - If the timer was armed again after its callback started, this
 *         returns 0 and keeps the deadline it was armed with. The caller must
 *         ensure timer_advance() never happens at the same time as the timer
 *         is armed from a different CPU.
 *       - timer_cancel_nosync() returning false does not stop a callback that
 *         returns TIMER_RESULT_REARM from re-arming the timer. Use
 *         timer_cancel() if the target timer re-arms itself from the callback.
 */
u64 timer_advance(struct timer *timer, u64 period_ns);

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
