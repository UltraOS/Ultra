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
