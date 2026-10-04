#include <private/time/timer.h>

#include <time/timer.h>
#include <time/time.h>
#include <time/units.h>

#include <common/atomic.h>
#include <common/bit.h>

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

struct timer_queue {
    struct spinlock lock;
    struct rb_root_cached timers;
    bool in_interrupt;
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

    return new_timer->deadline_ns < old_timer->deadline_ns;
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

static void queue_insert(
    struct timer_queue *queue, struct timer *timer, u64 deadline_ns
)
{
    if (timer->flags & TIMER_FLAG_QUEUED)
        queue_remove(queue, timer);

    timer->deadline_ns = deadline_ns;

    rb_node_insert_cached(&timer->node, &queue->timers, timer_expires_before);
    timer->flags |= TIMER_FLAG_QUEUED;
}

void timer_arm(struct timer *timer, u64 deadline_ns)
{
    struct timer_queue *queue;
    irq_state_t irq_state;

    irq_state = local_irq_state_save();
    queue = timer_lock_queue_for_arm(timer);
    queue_insert(queue, timer, deadline_ns);

    spin_unlock(&queue->lock);
    local_irq_state_restore(irq_state);
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

static void queue_run_due(struct timer_queue *queue, u64 now_ns)
{
    struct timer *timer;

    for (;;) {
        timer = queue_first(queue);
        if (timer == nullptr)
            break;
        if (timer->deadline_ns > now_ns)
            break;

        queue_remove(queue, timer);
        timer->flags |= TIMER_FLAG_RUNNING;
        spin_unlock(&queue->lock);

        timer->fn(timer);

        spin_lock(&queue->lock);
        timer->flags &= ~TIMER_FLAG_RUNNING;
    }
}

void timer_queue_expire(void)
{
    struct timer_queue *queue;

    queue = this_cpu_ptr(&s_queue);
    BUG_ON(!in_hard_irq());

    spin_lock(&queue->lock);
    queue->in_interrupt = true;

    queue_run_due(queue, ns_since_boot());

    queue->in_interrupt = false;
    spin_unlock(&queue->lock);
}
