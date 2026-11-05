#define MSG_FMT(msg) "time-keeper: " msg

#include <private/time/keeper.h>
#include <private/time/counter_device.h>

#include <bug.h>
#include <init_level.h>
#include <locking/spinlock.h>
#include <locking/seq.h>
#include <time/time.h>

#include <arch/cpu_helpers.h>

struct keeper_base {
    struct counter_device *cd;
    u64 ns;
    u64 ns_frac;
    u64 prev_cycles;
};

static struct keeper {
    struct spinlock lock;
    struct seqcount_latch time_seq;
    struct counter_device *cd;

    u64 ns;
    u64 ns_frac;
    u64 prev_cycles;

    // At least one of these is always consistent (time_seq decides which)
    struct keeper_base published[2];
} s_ctx = {
    .lock = SPINLOCK_INIT(s_ctx.lock),
    .time_seq = SEQCOUNT_LATCH_INIT(s_ctx.time_seq),
};

static void base_store(struct keeper_base *base)
{
    atomic_store_relaxed(&base->cd, s_ctx.cd);
    atomic_store_relaxed(&base->ns, s_ctx.ns);
    atomic_store_relaxed(&base->ns_frac, s_ctx.ns_frac);
    atomic_store_relaxed(&base->prev_cycles, s_ctx.prev_cycles);
}

static void publish(void)
{
    seqcount_latch_flip(&s_ctx.time_seq);
    base_store(&s_ctx.published[0]);

    seqcount_latch_flip(&s_ctx.time_seq);
    base_store(&s_ctx.published[1]);
}

static struct keeper_base *read_published(seq_t seq)
{
    return &s_ctx.published[seqcount_latch_copy(seq)];
}

static void do_tick(void)
{
    u64 cycles, delta_cycles, delta_ns;
    struct counter_device *cd = s_ctx.cd;

    if (unlikely(cd == nullptr))
        return;

    cycles = counter_device_read(cd);
    delta_cycles = counter_device_delta(cd, s_ctx.prev_cycles, cycles);
    delta_ns = counter_device_cycles_to_ns_frac(
        cd, delta_cycles, &s_ctx.ns_frac
    );

    s_ctx.ns += delta_ns;
    s_ctx.prev_cycles = cycles;

    publish();
}

void time_keeper_tick(void)
{
    irq_state_t state;

    state = spin_lock_irq_save(&s_ctx.lock);
    do_tick();
    spin_unlock_irq_restore(&s_ctx.lock, state);
}

void time_keeper_set_counter_device(struct counter_device *cd)
{
    irq_state_t state;
    u64 new_cycles = 0;

    state = spin_lock_irq_save(&s_ctx.lock);

    /*
     * The new counter is sampled before the old one is read for the last
     * time. Whatever passes in between is counted twice, which moves time
     * forward a little. The other order would lose it, and a reader in
     * that window would have seen a later time than the ones after it.
     */
    if (cd)
        new_cycles = counter_device_read(cd);

    // If we already had a device, accumulate its final time before dumping it
    if (s_ctx.cd) {
        u64 old_cycles, delta_cycles;

        old_cycles = counter_device_read(s_ctx.cd);
        delta_cycles = counter_device_delta(
            s_ctx.cd, s_ctx.prev_cycles, old_cycles
        );
        s_ctx.ns += counter_device_cycles_to_ns_frac(
            s_ctx.cd, delta_cycles, &s_ctx.ns_frac
        );

        // The fraction is in units of the old device's shift
        s_ctx.ns_frac = 0;
    }

    atomic_store_relaxed(&s_ctx.cd, cd);
    s_ctx.prev_cycles = new_cycles;

    publish();
    spin_unlock_irq_restore(&s_ctx.lock, state);
}

u64 ns_since_boot(void)
{
    seq_t seq;
    u64 ns, ns_frac, prev_cycles, now, delta_cycles;
    const struct keeper_base *base;
    struct counter_device *cd;

    do {
        seq = seqcount_latch_read_begin(&s_ctx.time_seq);
        base = read_published(seq);

        ns = atomic_load_relaxed(&base->ns);
        ns_frac = atomic_load_relaxed(&base->ns_frac);
        cd = atomic_load_relaxed(&base->cd);
        prev_cycles = atomic_load_relaxed(&base->prev_cycles);
        barrier_acquire();

        // No device installed, time is frozen at the accumulated value
        if (cd != nullptr)
            now = counter_device_read(cd);
    } while (seqcount_latch_read_retry(&s_ctx.time_seq, seq));

    if (cd == nullptr)
        return ns;

    delta_cycles = counter_device_delta(cd, prev_cycles, now);
    return ns + counter_device_cycles_to_ns_frac(cd, delta_cycles, &ns_frac);
}

void delay_ns(u64 ns)
{
    u64 deadline;

    if (WARN_ON(atomic_load_relaxed(&s_ctx.cd) == nullptr))
        // TODO: maybe add an arch_early_delay_ns helper
        return;

    deadline = ns_from_now(ns);
    while (ns_since_boot() < deadline)
        arch_cpu_relax();
}
