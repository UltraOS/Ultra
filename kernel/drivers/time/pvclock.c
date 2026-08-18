#include <arch/private/cpu.h>
#include <arch/private/tsc.h>

#include <time/pvclock.h>
#include <time/counter_device.h>

#include <free_after_init.h>

/*
 * From Documentation/virt/kvm/x86/msr.rst
 *     The conversion from tsc to nanoseconds involves an additional right
 *     shift by 32 bits.
 */
#define PVCLOCK_SHIFT 32

#define PVCLOCK_HZ NS_PER_SEC

static bool s_has_stable_bit;

DEFINE_PER_CPU(struct pvclock_vcpu_time_info*, g_this_cpu_time_info);

void pvclock_enable_stable_bit(void)
{
    s_has_stable_bit = true;
}

static u32 pvclock_read_start(struct pvclock_vcpu_time_info *info)
{
    u32 version;

    do {
        version = atomic_load_relaxed(&info->version);
    } while (version & 1);

    barrier_acquire();
    return version;
}

static u64 pvclock_cycles_to_ns(struct pvclock_vcpu_time_info *info, u64 tsc)
{
    u64 host_ns, elapsed_ticks, elapsed_ns;
    i8 shift;
    u32 mul;

    elapsed_ticks = tsc - atomic_load_relaxed(&info->host_tsc_timestamp);

    shift = atomic_load_relaxed(&info->guest_tsc_shift);
    if (shift < 0)
        elapsed_ticks >>= -shift;
    else
        elapsed_ticks <<= shift;

    mul = atomic_load_relaxed(&info->guest_tsc_mul);
    elapsed_ns = ((u128)elapsed_ticks * mul) >> PVCLOCK_SHIFT;

    host_ns = atomic_load_relaxed(&info->host_ns_since_boot);

    barrier_acquire();
    return host_ns + elapsed_ns;
}

static u64 pvclock_read_stablize(u64 this_read)
{
    static u64 s_last_read_ns;
    u64 last_read;

    last_read = atomic_load_relaxed(&s_last_read_ns);
    do {
        /*
         * The value we have read is behind a read some other vCPU has
         * performed before us. Ignore whatever we read and return that
         * instead.
         */
        if (this_read <= last_read)
            return last_read;
    } while (!atomic_cmpxchg_explicit(&s_last_read_ns, &last_read, this_read,
                                      MO_RELAXED, MO_RELAXED));
    return this_read;
}

u64 pvclock_read_from(struct pvclock_vcpu_time_info *info)
{
    u32 version;
    u64 ns_since_boot;
    u8 flags;

    do {
        version = pvclock_read_start(info);
        flags = atomic_load_relaxed(&info->flags);
        ns_since_boot = pvclock_cycles_to_ns(info, rdtsc());
    } while (atomic_load_relaxed(&info->version) != version);

    if (s_has_stable_bit && (flags & PVCLOCK_TSC_STABLE))
        // Stable pvclock, no further validation needed
        return ns_since_boot;

    /*
     * pvclock is not stable, so we aren't guaranteed to see synchornized
     * progress on all vCPUs. Debounce this read against a global monotonic
     * ns counter so we dont warp ourselves into the past.
     */
    return pvclock_read_stablize(ns_since_boot);
}

u64 pvclock_calculate_tsc_hz(struct pvclock_vcpu_time_info *info)
{
    u64 hz;

    hz = (PVCLOCK_HZ << PVCLOCK_SHIFT) / info->guest_tsc_mul;

    if (info->guest_tsc_shift < 0)
        hz <<= -info->guest_tsc_shift;
    else
        hz >>= info->guest_tsc_shift;

    return hz;
}

static u64 pvclock_read_cd(struct counter_device *cd)
{
    UNREFERENCED_PARAMETER(cd);
    return pvclock_read();
}

static struct counter_device s_pvclock_cd = {
    .name = "pv-clock",
    .read = pvclock_read_cd,
    .mask = COUNTER_MASK(64),
    .rating = TSC_PRECISE_RATING + 1,
};

error_t INIT_CODE pvclock_counter_register(void)
{
    /*
     * By default, we rate ourselves higher than TSC, so that we're used
     * unconditionally in VMs. However, if the host has explicitly enabled
     * the invariant bit, it promises that the TSC frequency will never change
     * between VCPU migrations (or that VM migration is disabled entirely),
     * thus we can use the TSC directly and skip pvclock entirely (since it's
     * a bit more expensive to read than the TSC and always involves 128-bit
     * math).
     */
    if (all_cpus_have(X86_FEATURE_TSC_INVARIANT))
        s_pvclock_cd.rating = TSC_PRECISE_RATING - 1;

    return counter_device_register(&s_pvclock_cd, PVCLOCK_HZ);
}
