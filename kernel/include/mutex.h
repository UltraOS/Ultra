#pragma once

#include <spinlock.h>

/*
 * A mutual exclusion lock for process context only. Holders are
 * allowed to block, so it must never be acquired from interrupt
 * context.
 *
 * Sleeping does not exist yet, so the current implementation is a
 * spinlock that must be free when taken, but every caller must
 * already honor the contract as if it could sleep.
 */
struct mutex {
    struct spinlock lock;
};

#define MUTEX_INIT(name) (struct mutex) { SPINLOCK_INIT(name.lock) }
#define DEFINE_MUTEX(name) struct mutex name = MUTEX_INIT(name);

void mutex_init(struct mutex*);

void mutex_lock(struct mutex*);
bool mutex_try_lock(struct mutex*);

void mutex_unlock(struct mutex*);
