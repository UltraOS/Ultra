#pragma once

#include <common/types.h>

struct spinlock {
    union {
        u32 control;

        struct {
        #ifdef ULTRA_ARCH_LITTLE_ENDIAN
            u16 current_ticket;
            u16 last_freed_ticket;
        #else
            u16 last_freed_ticket;
            u16 current_ticket;
        #endif

        };
    };
};

#define DEFINE_SPINLOCK(name) struct spinlock name = { { .control = 0 } };

static inline void spin_lock_init(struct spinlock *lock)
{
    lock->control = 0;
}

void spin_lock(struct spinlock*);
irq_state_t spin_lock_irq_save(struct spinlock*);

bool spin_try_lock(struct spinlock*);
bool spin_try_lock_irq_save(struct spinlock*);

void spin_unlock(struct spinlock*);
void spin_unlock_irq_restore(struct spinlock*, irq_state_t);

bool spin_is_locked(struct spinlock*);
bool spin_is_contended(struct spinlock*);
