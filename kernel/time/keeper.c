#define MSG_FMT(msg) "time-keeper: " msg

#include <private/time/keeper.h>
#include <private/time/counter_device.h>

#include <bug.h>
#include <init_level.h>
#include <spinlock.h>
#include <seq.h>
#include <time/time.h>

#include <arch/cpu_helpers.h>

static struct keeper {
    struct spinlock lock;
    struct seqcount time_seq;
    struct counter_device *cd;

    u64 ns;
    u64 prev_cycles;
} s_ctx = {
    .lock = SPINLOCK_INIT(&s_ctx.lock),
    .time_seq = SEQCOUNT_INIT(&s_ctx.time_seq),
};

static void do_tick(void)
{
    u64 cycles, delta_cycles, delta_ns;
    struct counter_device *cd = s_ctx.cd;

    if (unlikely(cd == nullptr))
        return;

    cycles = counter_device_read(cd);
    delta_cycles = counter_device_delta(cd, s_ctx.prev_cycles, cycles);
    delta_ns = counter_device_cycles_to_ns(cd, delta_cycles);

    seqcount_write_begin(&s_ctx.time_seq);

    atomic_store_relaxed(
        &s_ctx.ns,
        atomic_load_relaxed(&s_ctx.ns) + delta_ns
    );
    atomic_store_relaxed(&s_ctx.prev_cycles, cycles);

    seqcount_write_end(&s_ctx.time_seq);
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
    u64 cycles = 0;

    state = spin_lock_irq_save(&s_ctx.lock);
    seqcount_write_begin(&s_ctx.time_seq);

    // If we already had a device, accumulate its final time before dumping it
    if (s_ctx.cd) {
        u64 cycles, delta_cycles;

        cycles = counter_device_read(s_ctx.cd);
        delta_cycles = counter_device_delta(
            s_ctx.cd, s_ctx.prev_cycles, cycles
        );
        atomic_store_relaxed(
            &s_ctx.ns,
            atomic_load_relaxed(&s_ctx.ns) +
            counter_device_cycles_to_ns(s_ctx.cd, delta_cycles)
        );
    }

    if (cd)
        cycles = counter_device_read(cd);

    atomic_store_relaxed(&s_ctx.cd, cd);
    atomic_store_relaxed(&s_ctx.prev_cycles, cycles);

    seqcount_write_end(&s_ctx.time_seq);
    spin_unlock_irq_restore(&s_ctx.lock, state);
}

u64 ns_since_boot(void)
{
    u32 seq;
    u64 base, prev_cycles, now, delta_cycles, delta_ns;
    struct counter_device *cd;

    do {
        seq = seqcount_read_begin(&s_ctx.time_seq);

        base = atomic_load_relaxed(&s_ctx.ns);

        // No device installed, time is frozen at the accumulated value
        cd = atomic_load_relaxed(&s_ctx.cd);
        if (cd == nullptr)
            return base;

        prev_cycles = atomic_load_relaxed(&s_ctx.prev_cycles);
        barrier_acquire();

        now = counter_device_read(cd);
    } while (seqcount_read_retry(&s_ctx.time_seq, seq));

    delta_cycles = counter_device_delta(cd, prev_cycles, now);
    delta_ns = counter_device_cycles_to_ns(cd, delta_cycles);

    return base + delta_ns;
}

void delay_ns(u64 ns)
{
    u64 deadline;

    if (WARN_ON(atomic_load_relaxed(&s_ctx.cd) == nullptr))
        // TODO: maybe add an arch_early_delay_ns helper
        return;

    deadline = ns_since_boot() + ns;
    while (ns_since_boot() < deadline)
        arch_cpu_relax();
}
