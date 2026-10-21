#pragma once

#include <common/types.h>

#include <detail/locking/spinlock.h>

/*
 * An opaque spinlock, initialized with SPINLOCK_INIT() or spin_lock_init().
 * Use the spin_* helpers, all fields are internal.
 */
struct spinlock;

#define SPINLOCK_INIT(name) (struct spinlock) { { .control = 0 } }
#define DEFINE_SPINLOCK(name) struct spinlock name = SPINLOCK_INIT(name);

static inline void spin_lock_init(struct spinlock *lock)
{
    *lock = SPINLOCK_INIT(*lock);
}

void spin_lock(struct spinlock*);
irq_state_t spin_lock_irq_save(struct spinlock*);

bool spin_try_lock(struct spinlock*);
bool spin_try_lock_irq_save(struct spinlock*, irq_state_t *out_irq_state);

void spin_unlock(struct spinlock*);
void spin_unlock_irq_restore(struct spinlock*, irq_state_t);

bool spin_is_locked(struct spinlock*);
bool spin_is_contended(struct spinlock*);
