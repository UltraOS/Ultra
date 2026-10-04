#pragma once

#include <common/types.h>
#include <common/list.h>

#include <locking/spinlock.h>

struct task;
struct wait_entry;

enum wait_entry_flags : u32;

/*
 * Decides what waking the entry means. Returns whether the entry took
 * the wakeup, only those count against the exclusive ones woken.
 * Called with the queue locked.
 */
typedef bool (*wait_wake_fn_t)(struct wait_entry *entry);

bool wait_entry_wake_and_remove(struct wait_entry *entry);

struct wait_entry {
    enum wait_entry_flags flags;
    struct task *task;
    wait_wake_fn_t wake;
    struct list_link link;
};

struct wait_queue {
    struct spinlock lock;
    struct list_link entries;
};

// Same as wait_event_timeout(), but with an absolute 'deadline_ns'
#define wait_event_until(wq, condition, deadline_ns) ({                    \
    struct wait_entry wait_event_entry;                                    \
    bool wait_event_ok, wait_event_timed_out = false;                      \
    u64 wait_event_deadline_ns;                                            \
                                                                           \
    might_sleep();                                                         \
                                                                           \
    wait_event_ok = (condition);                                           \
                                                                           \
    if (!wait_event_ok) {                                                  \
        wait_event_deadline_ns = (deadline_ns);                            \
        wait_entry_init(&wait_event_entry, WAIT_ENTRY_FLAG_NONE);          \
                                                                           \
        for (;;) {                                                         \
            wait_queue_prepare(wq, &wait_event_entry);                     \
                                                                           \
            wait_event_ok = (condition);                                   \
            if (wait_event_ok || wait_event_timed_out)                     \
                break;                                                     \
                                                                           \
            wait_event_timed_out = schedule_until(wait_event_deadline_ns); \
        }                                                                  \
                                                                           \
        wait_queue_finish(wq, &wait_event_entry);                          \
    }                                                                      \
                                                                           \
    wait_event_ok;                                                         \
})
