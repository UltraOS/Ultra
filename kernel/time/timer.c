#include <private/time/timer.h>
#include <private/time/event_device.h>

#include <time/timer.h>
#include <time/event_device.h>
#include <time/time.h>
#include <time/units.h>

#include <common/atomic.h>
#include <common/bit.h>
#include <common/minmax.h>
#include <common/overflow.h>

#include <bug.h>
#include <per_cpu.h>
#include <preempt.h>
#include <locking/spinlock.h>
#include <irq_helpers.h>

#include <arch/cpu_helpers.h>

enum timer_flags : u32 {
    TIMER_FLAG_QUEUED = BIT_U32(0),
    TIMER_FLAG_RUNNING = BIT_U32(1),
};

#define MAX_PASSES_PER_INTERRUPT 3
#define MAX_BACKOFF_NS (100 * NS_PER_MS)

struct timer_queue {
    struct spinlock lock;
    struct rb_root_cached timers;
    struct event_device *dev;

    // The deadline the device is currently armed for, or NO_DEADLINE
    u64 armed_ns;
    bool in_interrupt;
    bool backing_off;
};

static DEFINE_PER_CPU(struct timer_queue, s_queue);

/*
 * A sentinel value that is written into timer->queue while it's being moved
 * between queues. We do this because we can't take both queue locks at the
 * same time since that may cause a deadlock.
 */
static struct timer_queue s_moving_queue;

static bool timer_expires_before(
    const struct rb_node *new_node, const struct rb_node *old_node
)
{
    const struct timer *new_timer, *old_timer;

    new_timer = rb_entry(new_node, struct timer, node);
    old_timer = rb_entry(old_node, struct timer, node);

    return new_timer->hard_deadline_ns < old_timer->hard_deadline_ns;
}

static struct timer *queue_first(struct timer_queue *queue)
{
    struct rb_node *node;

    node = rb_first_cached(&queue->timers);
    if (node == nullptr)
        return nullptr;

    return rb_entry(node, struct timer, node);
}

static void queue_remove(struct timer_queue *queue, struct timer *timer)
{
    rb_node_remove_cached(&timer->node, &queue->timers);
    timer->flags &= ~TIMER_FLAG_QUEUED;
}

static u64 queue_next_deadline(struct timer_queue *queue)
{
    struct timer *first;

    first = queue_first(queue);
    if (first == nullptr)
        return NO_DEADLINE;

    return first->hard_deadline_ns;
}

static void queue_arm_device(struct timer_queue *queue, u64 deadline_ns)
{
    struct event_device *dev = queue->dev;

    queue->armed_ns = deadline_ns;

    if (deadline_ns == NO_DEADLINE)
        dev->disarm(dev);
    else
        event_device_arm(dev, deadline_ns);
}

static void queue_arm(struct timer_queue *queue)
{
    u64 deadline_ns;

    if (queue->dev == nullptr)
        return;

    deadline_ns = queue_next_deadline(queue);
    if (deadline_ns != queue->armed_ns)
        queue_arm_device(queue, deadline_ns);
}

static struct timer_queue *timer_lock_queue(
    struct timer *timer, irq_state_t *out_irq_state
)
{
    struct timer_queue *queue;
    irq_state_t irq_state;

    for (;;) {
        queue = atomic_load_acquire(&timer->queue);
        if (queue == nullptr)
            return nullptr;
        if (queue == &s_moving_queue) {
            arch_cpu_relax();
            continue;
        }

        irq_state = spin_lock_irq_save(&queue->lock);

        // It may have been armed on another CPU while we took the lock
        if (likely(queue == atomic_load_relaxed(&timer->queue))) {
            *out_irq_state = irq_state;
            return queue;
        }

        spin_unlock_irq_restore(&queue->lock, irq_state);
    }
}

/*
 * Lock and return the queue a timer armed on this CPU goes on. A timer
 * whose callback runs on another CPU stays on that CPU's queue.
 */
static struct timer_queue *timer_lock_queue_for_arm(struct timer *timer)
{
    struct timer_queue *queue, *old_queue;

    queue = this_cpu_ptr(&s_queue);

    for (;;) {
        old_queue = atomic_load_acquire(&timer->queue);
        if (old_queue == &s_moving_queue) {
            arch_cpu_relax();
            continue;
        }

        if (old_queue == nullptr) {
            spin_lock(&queue->lock);

            // Another CPU may arm this fresh timer at the same time
            if (atomic_cmpxchg_acq_rel(&timer->queue, &old_queue, queue))
                return queue;

            spin_unlock(&queue->lock);
            continue;
        }

        spin_lock(&old_queue->lock);
        if (old_queue != atomic_load_relaxed(&timer->queue)) {
            spin_unlock(&old_queue->lock);
            continue;
        }

        if (old_queue == queue)
            return queue;

        if (timer->flags & TIMER_FLAG_RUNNING) {
            // This timer is already running on another CPU, so leave it there
            BUG_ON(!old_queue->in_interrupt);
            return old_queue;
        }

        if (timer->flags & TIMER_FLAG_QUEUED)
            queue_remove(old_queue, timer);

        /*
         * Once the old lock is dropped nothing covers the timer, so the other
         * CPUs wait until it is on the new queue.
         */
        atomic_store_relaxed(&timer->queue, &s_moving_queue);
        spin_unlock(&old_queue->lock);

        spin_lock(&queue->lock);
        atomic_store_release(&timer->queue, queue);
        return queue;
    }
}

void timer_init(struct timer *timer, timer_fn_t fn)
{
    *timer = TIMER_INIT(*timer, fn);
}

// The interrupt handler re-arms the device at the end, after the last callback
static bool queue_should_defer_arming(struct timer_queue *queue)
{
    /*
     * During a back-off the device stays armed for the end of it, a
     * timer armed meanwhile runs then at the latest.
     */
    return queue->in_interrupt || queue->backing_off;
}

static bool queue_should_arm(
    struct timer_queue *queue, struct timer *timer, bool lazy
)
{
    if (queue_first(queue) != timer)
        return false;
    if (queue_should_defer_arming(queue))
        return false;

    // The interrupt for the earlier deadline arms the device for this one
    if (lazy && queue->armed_ns <= timer->hard_deadline_ns)
        return false;

    return true;
}

static void queue_insert(
    struct timer_queue *queue, struct timer *timer,
    u64 deadline_ns, u64 hard_deadline_ns
)
{
    if (timer->flags & TIMER_FLAG_QUEUED)
        queue_remove(queue, timer);

    timer->deadline_ns = deadline_ns;
    timer->hard_deadline_ns = hard_deadline_ns;

    rb_node_insert_cached(&timer->node, &queue->timers, timer_expires_before);
    timer->flags |= TIMER_FLAG_QUEUED;
}

static void queue_requeue(
    struct timer_queue *queue, struct timer *timer, u64 now_ns
)
{
    if (timer->flags & TIMER_FLAG_QUEUED)
        return;

    /*
     * Make sure the callback didn't forget to advance the timer, otherwise it
     * would run again and again within this pass and cause us to hang.
     */
    BUG_ON(timer->deadline_ns <= now_ns);
    queue_insert(queue, timer, timer->deadline_ns, timer->hard_deadline_ns);
}

static void do_timer_arm(
    struct timer *timer, u64 deadline_ns, u64 slack_ns, bool lazy
)
{
    struct timer_queue *queue;
    irq_state_t irq_state;
    u64 hard_deadline_ns;

    /*
     * A hard deadline that wraps around would sit at the front of the queue
     * and keep every timer behind it from running
     */
    BUG_ON(check_add_overflow(deadline_ns, slack_ns, &hard_deadline_ns));

    irq_state = local_irq_state_save();
    queue = timer_lock_queue_for_arm(timer);
    queue_insert(queue, timer, deadline_ns, hard_deadline_ns);

    if (queue_should_arm(queue, timer, lazy))
        queue_arm(queue);

    spin_unlock(&queue->lock);
    local_irq_state_restore(irq_state);
}

void timer_arm_with_slack(struct timer *timer, u64 deadline_ns, u64 slack_ns)
{
    do_timer_arm(timer, deadline_ns, slack_ns, false);
}

void timer_arm_lazy(struct timer *timer, u64 deadline_ns)
{
    do_timer_arm(timer, deadline_ns, 0, true);
}

bool timer_cancel_nosync(struct timer *timer)
{
    struct timer_queue *queue;
    irq_state_t irq_state;
    bool running;

    queue = timer_lock_queue(timer, &irq_state);
    if (queue == nullptr)
        return true;

    if (timer->flags & TIMER_FLAG_QUEUED)
        queue_remove(queue, timer);
    running = timer->flags & TIMER_FLAG_RUNNING;

    spin_unlock_irq_restore(&queue->lock, irq_state);
    return !running;
}

void timer_cancel(struct timer *timer)
{
    while (!timer_cancel_nosync(timer)) {
        BUG_ON(in_hard_irq());
        arch_cpu_relax();
    }
}

u64 timer_advance(struct timer *timer, u64 period_ns)
{
    u64 now_ns, num_periods, advance_ns;

    BUG_ON(period_ns == 0);

    /*
     * It was armed again since its callback started, so that deadline stands.
     * Moving the deadline of a queued timer would break the order of the queue.
     */
    if (timer->flags & TIMER_FLAG_QUEUED)
        return 0;

    now_ns = ns_since_boot();
    if (timer->deadline_ns > now_ns)
        return 0;

    num_periods = (now_ns - timer->deadline_ns) / period_ns + 1;

    /*
     * A hard deadline that wraps around would sit at the front of the queue
     * and keep every timer behind it from running
     */
    BUG_ON(check_mul_overflow(num_periods, period_ns, &advance_ns));
    BUG_ON(check_add_overflow(
        timer->hard_deadline_ns, advance_ns, &timer->hard_deadline_ns
    ));

    // The deadline is never after the hard deadline, so this can't overflow
    timer->deadline_ns += advance_ns;

    return num_periods;
}

void timer_queue_set_event_device(struct event_device *dev)
{
    struct timer_queue *queue;
    irq_state_t irq_state;

    irq_state = local_irq_state_save();
    queue = this_cpu_ptr(&s_queue);

    spin_lock(&queue->lock);
    queue->dev = dev;
    queue_arm_device(queue, queue_next_deadline(queue));
    spin_unlock(&queue->lock);

    local_irq_state_restore(irq_state);
}

static void queue_run_due(struct timer_queue *queue, u64 now_ns)
{
    struct timer *timer;
    enum timer_result result;

    for (;;) {
        timer = queue_first(queue);
        if (timer == nullptr)
            break;
        if (timer->deadline_ns > now_ns)
            break;

        queue_remove(queue, timer);
        timer->flags |= TIMER_FLAG_RUNNING;
        spin_unlock(&queue->lock);

        result = timer->fn(timer);

        spin_lock(&queue->lock);
        timer->flags &= ~TIMER_FLAG_RUNNING;

        if (result == TIMER_RESULT_REARM)
            queue_requeue(queue, timer, now_ns);
    }
}

void timer_queue_expire(void)
{
    struct timer_queue *queue;
    struct timer *first;
    u64 entry_ns, now_ns, backoff_ns;
    u32 pass = 0;

    queue = this_cpu_ptr(&s_queue);
    BUG_ON(!in_hard_irq());

    spin_lock(&queue->lock);
    queue->in_interrupt = true;
    queue->backing_off = false;
    queue->armed_ns = NO_DEADLINE;

    entry_ns = ns_since_boot();
    now_ns = entry_ns;

    for (;;) {
        queue_run_due(queue, now_ns);

        first = queue_first(queue);
        now_ns = ns_since_boot();

        if (first == nullptr || first->hard_deadline_ns > now_ns) {
            queue_arm(queue);
            break;
        }

        if (++pass < MAX_PASSES_PER_INTERRUPT)
            continue;

        /*
         * Timers come due faster than their callbacks finish. Leave the
         * interrupt for as long as we have spent in it, so that something
         * other than timer callbacks gets to run.
         */
        backoff_ns = MIN(now_ns - entry_ns, MAX_BACKOFF_NS);
        queue_arm_device(queue, now_ns + backoff_ns);
        queue->backing_off = true;
        break;
    }

    queue->in_interrupt = false;
    spin_unlock(&queue->lock);
}
