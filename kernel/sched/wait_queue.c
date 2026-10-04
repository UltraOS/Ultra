#include <sched/wait_queue.h>

#include <private/sched/sched.h>
#include <private/sched/wait_queue.h>

#include <bug.h>

bool wait_entry_wake_and_remove(struct wait_entry *entry)
{
    if (!wake_up_task(entry->task, TASK_STATE_UNINTERRUPTIBLE))
        return false;

    list_remove(&entry->link);
    return true;
}

void wait_queue_init(struct wait_queue *wq)
{
    *wq = WAIT_QUEUE_INIT(*wq);
}

void wait_entry_init(struct wait_entry *entry, enum wait_entry_flags flags)
{
    *entry = WAIT_ENTRY_INIT(*entry, flags);
}

void wait_queue_prepare_unlocked(
    struct wait_queue *wq, struct wait_entry *entry
)
{
    BUG_ON(!spin_is_locked(&wq->lock));

    if (list_is_empty(&entry->link)) {
        if (entry->flags & WAIT_ENTRY_FLAG_EXCLUSIVE)
            list_insert_prev(&wq->entries, &entry->link);
        else
            list_insert_next(&wq->entries, &entry->link);
    }

    set_current_state(TASK_STATE_UNINTERRUPTIBLE);
}

void wait_queue_prepare(struct wait_queue *wq, struct wait_entry *entry)
{
    irq_state_t irq_state;

    irq_state = spin_lock_irq_save(&wq->lock);
    wait_queue_prepare_unlocked(wq, entry);
    spin_unlock_irq_restore(&wq->lock, irq_state);
}

void wait_queue_remove_unlocked(
    struct wait_queue *wq, struct wait_entry *entry
)
{
    BUG_ON(!spin_is_locked(&wq->lock));
    list_remove(&entry->link);
}

void wait_queue_finish(struct wait_queue *wq, struct wait_entry *entry)
{
    irq_state_t irq_state;

    set_current_state(TASK_STATE_RUNNING);

    irq_state = spin_lock_irq_save(&wq->lock);
    wait_queue_remove_unlocked(wq, entry);
    spin_unlock_irq_restore(&wq->lock, irq_state);
}

void wait_queue_wake_unlocked(struct wait_queue *wq, u32 num_exclusive)
{
    struct wait_entry *entry, *next;
    enum wait_entry_flags flags;

    BUG_ON(!spin_is_locked(&wq->lock));

    list_for_each_entry_safe(entry, next, &wq->entries, link) {
        flags = entry->flags;

        if (!entry->wake(entry))
            continue;
        if (!(flags & WAIT_ENTRY_FLAG_EXCLUSIVE))
            continue;

        if (--num_exclusive == 0)
            break;
    }
}

static void do_wake(struct wait_queue *wq, u32 num_exclusive)
{
    irq_state_t irq_state;

    irq_state = spin_lock_irq_save(&wq->lock);
    wait_queue_wake_unlocked(wq, num_exclusive);
    spin_unlock_irq_restore(&wq->lock, irq_state);
}

void wait_queue_wake(struct wait_queue *wq)
{
    do_wake(wq, 1);
}

void wait_queue_wake_all(struct wait_queue *wq)
{
    do_wake(wq, 0);
}
