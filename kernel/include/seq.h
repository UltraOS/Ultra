#pragma once

#include <common/types.h>
#include <common/atomic.h>

#include <spinlock.h>
#include <arch/cpu_helpers.h>

#include <irq_helpers.h>

struct seqcount {
    u32 sequence;
};

#define SEQCOUNT_INIT(name) (struct seqcount) { .sequence = 0 }

static inline void seqcount_init(struct seqcount *s)
{
    atomic_store_relaxed(&s->sequence, 0);
}

static inline void seqcount_write_begin(struct seqcount *s)
{
    atomic_fetch_add(&s->sequence, 1, MO_ACQ_REL);
}

static inline void seqcount_write_end(struct seqcount *s)
{
    atomic_fetch_add(&s->sequence, 1, MO_RELEASE);
}

static inline irq_state_t seqcount_write_begin_irq_save(struct seqcount *s)
{
    irq_state_t state;

    state = irq_state_save();
    seqcount_write_begin(s);

    return state;
}

static inline void seqcount_write_end_irq_restore(
    struct seqcount *s, irq_state_t state
)
{
    seqcount_write_end(s);
    irq_state_restore(state);
}

static inline u32 seqcount_read_begin(const struct seqcount *s)
{
    u32 seq;

    for (;;) {
        seq = atomic_load_acquire(&s->sequence);

        if (seq & 1) {
            arch_cpu_relax();
            continue;
        }

        return seq;
    }
}

static inline bool seqcount_read_retry(const struct seqcount *s, u32 start_seq)
{
    barrier_acquire();
    return atomic_load_relaxed(&s->sequence) != start_seq;
}

typedef struct {
    struct seqcount seqcount;
    struct spinlock lock;
} seqlock;

#define SEQLOCK_INIT(name) (struct seqlock) \
    { .seqcount = SEQCOUNT_INIT(name), .lock = SPINLOCK_INIT(name) }

static inline void seqlock_write_lock(seqlock *sl)
{
    spin_lock(&sl->lock);
    seqcount_write_begin(&sl->seqcount);
}

static inline void seqlock_write_unlock(seqlock *sl)
{
    seqcount_write_end(&sl->seqcount);
    spin_unlock(&sl->lock);
}

static inline irq_state_t seqlock_write_lock_irq_save(seqlock *sl)
{
    irq_state_t state;

    state = spin_lock_irq_save(&sl->lock);
    seqcount_write_begin(&sl->seqcount);

    return state;
}

static inline void seqlock_write_unlock_irq_restore(
    seqlock *sl, irq_state_t state
)
{
    seqcount_write_end(&sl->seqcount);
    spin_unlock_irq_restore(&sl->lock, state);
}
