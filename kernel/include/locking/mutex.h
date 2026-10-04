#pragma once

#include <common/types.h>
#include <common/list.h>

#include <locking/spinlock.h>

#include <detail/locking/mutex.h>

/*
 * A mutual exclusion lock for process context only. Holders are
 * allowed to block, so it must never be acquired from interrupt
 * context.
 */
struct mutex;

#define MUTEX_INIT(name) (struct mutex) {         \
    .wait_lock = SPINLOCK_INIT((name).wait_lock), \
    .waiters = LIST_INIT((name).waiters),         \
}
#define DEFINE_MUTEX(name) struct mutex name = MUTEX_INIT(name);

void mutex_init(struct mutex*);

void mutex_lock(struct mutex*);
bool mutex_try_lock(struct mutex*);

void mutex_unlock(struct mutex*);
