#pragma once

#include <common/types.h>
#include <common/list.h>
#include <common/bit.h>

#include <sched/sched.h>
#include <sched/task.h>
#include <locking/spinlock.h>

#include <detail/sched/sched.h>
#include <detail/sched/wait_queue.h>

/*
 * A list of tasks waiting for something, initialized with
 * WAIT_QUEUE_INIT() or wait_queue_init(). All fields are internal.
 */
struct wait_queue;

#define WAIT_QUEUE_INIT(name) (struct wait_queue) { \
    .lock = SPINLOCK_INIT((name).lock),             \
    .entries = LIST_INIT((name).entries),           \
}
#define DEFINE_WAIT_QUEUE(name) struct wait_queue name = WAIT_QUEUE_INIT(name)

void wait_queue_init(struct wait_queue *wq);

/*
 * A task's place on a wait queue, set up with WAIT_ENTRY_INIT() or
 * wait_entry_init() and queued by wait_queue_prepare(). All fields are
 * internal.
 */
struct wait_entry;

enum wait_entry_flags : u32 {
    // An ordinary (shared) waiter
    WAIT_ENTRY_FLAG_NONE = 0,

    /*
     * An exclusive waiter, a normal wakeup stops at the first such entry that
     * accepts it
     */
    WAIT_ENTRY_FLAG_EXCLUSIVE = BIT_U32(0),
};

#define WAIT_ENTRY_INIT(name, entry_flags) (struct wait_entry) { \
    .flags = (entry_flags),                                      \
    .task = current,                                             \
    .wake = wait_entry_wake_and_remove,                          \
    .link = LIST_INIT((name).link),                              \
}
#define DEFINE_WAIT_ENTRY(name, entry_flags)                    \
    struct wait_entry name = WAIT_ENTRY_INIT(name, entry_flags)

void wait_entry_init(struct wait_entry *entry, enum wait_entry_flags flags);

/*
 * Queue 'entry' if it is not queued yet and mark the current task as
 * sleeping. Check the condition after this and before schedule(), so
 * that a wakeup in between is not lost.
 */
void wait_queue_prepare(struct wait_queue *wq, struct wait_entry *entry);

/*
 * Ends the wait once the condition holds. Marks the current task running
 * again and takes 'entry' off the queue. Call it exactly once after the
 * last wait_queue_prepare(), whether or not the task ever slept.
 */
void wait_queue_finish(struct wait_queue *wq, struct wait_entry *entry);

// Wake every shared and at most one exclusive waiter
void wait_queue_wake(struct wait_queue *wq);

// Wake everyone
void wait_queue_wake_all(struct wait_queue *wq);

// Sleep until 'condition' is true, it is rechecked after every wakeup
#define wait_event(wq, condition) do {                        \
    struct wait_entry wait_event_entry;                       \
                                                              \
    might_sleep();                                            \
                                                              \
    if (condition)                                            \
        break;                                                \
                                                              \
    wait_entry_init(&wait_event_entry, WAIT_ENTRY_FLAG_NONE); \
                                                              \
    for (;;) {                                                \
        wait_queue_prepare(wq, &wait_event_entry);            \
                                                              \
        if (condition)                                        \
            break;                                            \
                                                              \
        schedule();                                           \
    }                                                         \
                                                              \
    wait_queue_finish(wq, &wait_event_entry);                 \
} while (0)
