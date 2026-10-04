#pragma once

#include <common/types.h>
#include <common/atomic.h>

#include <locking/spinlock.h>
#include <arch/cpu_helpers.h>

#include <irq_helpers.h>

typedef u32 seq_t;

struct seqcount {
    seq_t sequence;
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

    state = local_irq_state_save();
    seqcount_write_begin(s);

    return state;
}

static inline void seqcount_write_end_irq_restore(
    struct seqcount *s, irq_state_t state
)
{
    seqcount_write_end(s);
    local_irq_state_restore(state);
}

static inline seq_t seqcount_read_begin(const struct seqcount *s)
{
    seq_t seq;

    for (;;) {
        seq = atomic_load_acquire(&s->sequence);

        if (seq & 1) {
            arch_cpu_relax();
            continue;
        }

        return seq;
    }
}

static inline bool seqcount_read_retry(
    const struct seqcount *s, seq_t start_seq
)
{
    barrier_acquire();
    return atomic_load_relaxed(&s->sequence) != start_seq;
}

/*
 * A sequence count used as a latch protects two copies of the same
 * data. The writer flips, updates copy 0, flips again and updates
 * copy 1. A reader picks the copy seqcount_latch_copy() names, which
 * is the one not being written, and retries with
 * seqcount_latch_read_retry(). It never waits for the writer, so it
 * may interrupt one on its own CPU. Writers must exclude each other.
 */
struct seqcount_latch {
    struct seqcount seqcount;
};

#define SEQCOUNT_LATCH_INIT(name) \
    (struct seqcount_latch) { .seqcount = SEQCOUNT_INIT((name).seqcount) }

static inline void seqcount_latch_init(struct seqcount_latch *latch)
{
    *latch = SEQCOUNT_LATCH_INIT(*latch);
}

static inline void seqcount_latch_flip(struct seqcount_latch *latch)
{
    struct seqcount *s = &latch->seqcount;

    barrier_release();
    atomic_store_relaxed(&s->sequence, atomic_load_relaxed(&s->sequence) + 1);
    barrier_release();
}

static inline seq_t seqcount_latch_read_begin(
    const struct seqcount_latch *latch
)
{
    return atomic_load_acquire(&latch->seqcount.sequence);
}

static inline size_t seqcount_latch_copy(seq_t seq)
{
    return seq & 1;
}

static inline bool seqcount_latch_read_retry(
    const struct seqcount_latch *latch, seq_t start_seq
)
{
    return seqcount_read_retry(&latch->seqcount, start_seq);
}

struct seqlock {
    struct seqcount seqcount;
    struct spinlock lock;
};

#define SEQLOCK_INIT(name) (struct seqlock) {   \
    .seqcount = SEQCOUNT_INIT((name).seqcount), \
    .lock = SPINLOCK_INIT((name).lock),         \
}

static inline void seqlock_write_lock(struct seqlock *sl)
{
    spin_lock(&sl->lock);
    seqcount_write_begin(&sl->seqcount);
}

static inline void seqlock_write_unlock(struct seqlock *sl)
{
    seqcount_write_end(&sl->seqcount);
    spin_unlock(&sl->lock);
}

static inline irq_state_t seqlock_write_lock_irq_save(struct seqlock *sl)
{
    irq_state_t state;

    state = spin_lock_irq_save(&sl->lock);
    seqcount_write_begin(&sl->seqcount);

    return state;
}

static inline void seqlock_write_unlock_irq_restore(
    struct seqlock *sl, irq_state_t state
)
{
    seqcount_write_end(&sl->seqcount);
    spin_unlock_irq_restore(&sl->lock, state);
}
