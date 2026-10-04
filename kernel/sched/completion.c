#include <sched/completion.h>
#include <private/sched/wait_queue.h>

#define DONE_PERMANENT UINT32_MAX

void completion_init(struct completion *completion)
{
    *completion = COMPLETION_INIT(*completion);
}

void complete(struct completion *completion)
{
    struct wait_queue *wq = &completion->wq;
    irq_state_t irq_state;

    irq_state = spin_lock_irq_save(&wq->lock);

    if (completion->done != DONE_PERMANENT)
        completion->done++;
    wait_queue_wake_unlocked(wq, 1);

    spin_unlock_irq_restore(&wq->lock, irq_state);
}

void complete_all(struct completion *completion)
{
    struct wait_queue *wq = &completion->wq;
    irq_state_t irq_state;

    irq_state = spin_lock_irq_save(&wq->lock);

    completion->done = DONE_PERMANENT;
    wait_queue_wake_unlocked(wq, 0);

    spin_unlock_irq_restore(&wq->lock, irq_state);
}

static bool do_wait(struct completion *completion, u64 deadline_ns)
{
    struct wait_queue *wq = &completion->wq;
    struct wait_entry entry;
    irq_state_t irq_state;
    bool timed_out = false;

    might_sleep();

    irq_state = spin_lock_irq_save(&wq->lock);

    if (completion->done == 0) {
        wait_entry_init(&entry, WAIT_ENTRY_FLAG_EXCLUSIVE);

        for (;;) {
            wait_queue_prepare_unlocked(wq, &entry);
            spin_unlock_irq_restore(&wq->lock, irq_state);

            timed_out = schedule_until(deadline_ns);

            irq_state = spin_lock_irq_save(&wq->lock);

            if (completion->done != 0 || timed_out)
                break;
        }

        wait_queue_remove_unlocked(wq, &entry);
    }

    if (completion->done == 0) {
        spin_unlock_irq_restore(&wq->lock, irq_state);
        return false;
    }

    if (completion->done != DONE_PERMANENT)
        completion->done--;

    spin_unlock_irq_restore(&wq->lock, irq_state);
    return true;
}

void wait_for_completion(struct completion *completion)
{
    do_wait(completion, NO_DEADLINE);
}

bool wait_for_completion_timeout(struct completion *completion, u64 timeout_ns)
{
    return do_wait(completion, ns_from_now(timeout_ns));
}
