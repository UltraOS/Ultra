#pragma once

#include <common/types.h>

struct spinlock {
    union {
        u32 control;

        struct {
        #ifdef ULTRA_ARCH_LITTLE_ENDIAN
            u16 last_freed_ticket;
            u16 current_ticket;
        #else
            u16 current_ticket;
            u16 last_freed_ticket;
        #endif

        };
    };
};

#define SPINLOCK_INIT(name) (struct spinlock) { { .control = 0 } }
#define DEFINE_SPINLOCK(name) struct spinlock name = SPINLOCK_INIT(name);

static inline void spin_lock_init(struct spinlock *lock)
{
    lock->control = 0;
}

void spin_lock(struct spinlock*);
irq_state_t spin_lock_irq_save(struct spinlock*);

bool spin_try_lock(struct spinlock*);
bool spin_try_lock_irq_save(struct spinlock*, irq_state_t *out_irq_state);

void spin_unlock(struct spinlock*);
void spin_unlock_irq_restore(struct spinlock*, irq_state_t);

bool spin_is_locked(struct spinlock*);
bool spin_is_contended(struct spinlock*);
