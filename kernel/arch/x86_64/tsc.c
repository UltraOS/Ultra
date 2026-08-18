#include "arch/registers.h"
#define MSG_FMT(x) "tsc: " x

#include <arch/private/cpu.h>
#include <arch/private/tsc.h>
#include <arch/private/apic.h>
#include <arch/cpu_helpers.h>

#include <log.h>
#include <bug.h>
#include <free_after_init.h>
#include <init_level.h>

#include <time/units.h>
#include <private/time/counter_device.h>

static struct tsc_ctx {
    const char *reported_by;
    u64 hz;
    bool precise;
    bool registered;
} s_tsc_ctx;

static u64 tsc_read_cd(struct counter_device *cd)
{
    UNREFERENCED_PARAMETER(cd);
    return rdtsc();
}

/*
 * Approximate TSC, either we used the CPUID 0x15+0x16 so we're slightly off,
 * or the early 50ms calibration, which can sometimes be off as well, thus
 * we call this an "approximate" frequency TSC.
 */
static struct counter_device s_approximate_tsc_cd = {
    .name = "tsc-approximate",
    .read = tsc_read_cd,
    .mask = COUNTER_MASK(64),
    .rating = TSC_APPROXIMATE_RATING,
};

/*
 * Precise TSC, either we know the frequency from CPUID 0x15, or we're in a VM
 * and the hypervisor has provided us with the exact frequency, or we have
 * performed late 1s-long calibration so we have a pretty good guarantee that
 * this one is "precise".
 */
static struct counter_device s_precise_tsc_cd = {
    .name = "tsc-precise",
    .read = tsc_read_cd,
    .mask = COUNTER_MASK(64),
    .rating = TSC_PRECISE_RATING,
};

static void INIT_CODE tsc_set_frequency(
    u64 hz, bool precise, const char *reported_by
)
{
    if (s_tsc_ctx.hz) {
        if (!precise)
            // One imprecise guess is good enough
            return;

        // A precise guess is expected to match a previous precise guess
        if (s_tsc_ctx.precise)
            BUG_ON(hz != s_tsc_ctx.hz);

        return;
    }

    s_tsc_ctx.precise = precise;
    s_tsc_ctx.hz = hz;
    s_tsc_ctx.reported_by = reported_by;

    pr_info(
        "frequency set to %llu.%03llu MHz (%s, reported by %s)\n",
        hz / MHZ, hz % MHZ, precise ? "precise" : "approximate",
        reported_by
    );
}

static void INIT_CODE tsc_clocksource_register(void)
{
    if (s_tsc_ctx.registered)
        return;

    s_tsc_ctx.registered = true;

    if (s_tsc_ctx.precise) {
        counter_device_register(&s_precise_tsc_cd, s_tsc_ctx.hz);
        return;
    }

    counter_device_register(&s_approximate_tsc_cd, s_tsc_ctx.hz);
}

void INIT_CODE tsc_set_known_frequency(u64 hz, const char *reported_by)
{
    tsc_set_frequency(hz, true, reported_by);
    tsc_clocksource_register();
}

#define CPUID_TSC_AND_NOMINAL_CORE_CRYSTAL_CLOCK 0x15
#define CPUID_PROCESSOR_FREQUENCY_INFORMATION 0x16

static INIT_CODE void tsc_try_detect_frequency_from_cpuid(void)
{
    u32 denominator, numerator, nominal_art_freq_hz, unused;
    u64 crystal_hz, tsc_hz;
    bool precise = true;
    const char *reported_by = "cpuid 0x15";

    // Don't even bother on AMD, it's always all zeroes there
    if (g_cpu_info.vendor != X86_VENDOR_INTEL ||
        g_cpu_info.max_cpuid < CPUID_TSC_AND_NOMINAL_CORE_CRYSTAL_CLOCK)
        return;

    cpuid_inline(
        CPUID_TSC_AND_NOMINAL_CORE_CRYSTAL_CLOCK,
        &denominator, &numerator, &nominal_art_freq_hz, &unused
    );
    if (denominator == 0 || numerator == 0)
        return;

    if (nominal_art_freq_hz) {
        crystal_hz = nominal_art_freq_hz;
        tsc_hz = crystal_hz * numerator / denominator;
    } else if (g_cpu_info.max_cpuid >= CPUID_PROCESSOR_FREQUENCY_INFORMATION) {
        u32 cpu_freq_mhz;
        u64 cpu_freq_hz;

        cpuid_inline(
            CPUID_PROCESSOR_FREQUENCY_INFORMATION,
            &cpu_freq_mhz, &unused, &unused, &unused
        );
        if (cpu_freq_mhz == 0)
            return;

        cpu_freq_hz = cpu_freq_mhz * MHZ;
        crystal_hz = cpu_freq_hz * denominator / numerator;
        tsc_hz = cpu_freq_hz;

        /*
         * CPUID 0x16 is used for display/marketing purposes only, thus the
         * frequency it reports is only approximate. For example, on my HP
         * laptop the reported 0x16 frequency is 1900.000 MHz, however,
         * the real TSC frequency is 1896.000 MHz (a 24 MHz crystal with
         * a multiplier of 79 (158 / 2))
         */
        precise = false;
        reported_by = "cpuid 0x15;0x16";
    } else {
        return;
    }

    tsc_set_frequency(tsc_hz, precise, reported_by);
    apic_set_known_frequency(crystal_hz);
}

#define EARLY_CALIBRATE_MS 50

static void INIT_CODE early_tsc_calibrate(void)
{
    u64 tsc_start, tsc_end, tsc_delta;
    u64 ref_start, ref_now, ref_delta, ref_ns;
    u64 target_ns = EARLY_CALIBRATE_MS * NS_PER_MS;
    struct counter_device *ref_cd;

    ref_cd = current_counter_device();
    if (unlikely(ref_cd == nullptr))
        return;

    ref_start = counter_device_read(ref_cd);
    tsc_start = rdtsc();

    do {
        arch_cpu_relax();
        ref_now = counter_device_read(ref_cd);
        ref_delta = (ref_now - ref_start) & ref_cd->mask;

        if (unlikely(ref_delta > ref_cd->max_acceptable_cycles_delta)) {
            /*
             * Either we're in a VM and got preempted for a huge amount of time
             * or this counter is buggy. Just don't register TSC for now
             */
            pr_warn("early calibration failed\n");
            return;
        }

        ref_ns = counter_device_cycles_to_ns(ref_cd, ref_delta);
    } while (ref_ns < target_ns);

    tsc_end = rdtsc();
    tsc_delta = tsc_end - tsc_start;

    tsc_set_frequency(
        (tsc_delta * NS_PER_SEC) / ref_ns, false, "early calibration"
    );
}

static error_t INIT_CODE x86_early_tsc_setup(void)
{
    if (s_tsc_ctx.registered)
        return EOK;

    if (unlikely(!all_cpus_have(X86_FEATURE_TSC))) {
        pr_warn("not supported on this CPU(?)");
        return EOK;
    }

    if (!all_cpus_have(X86_FEATURE_TSC_RELIABLE)) {
        pr_warn("not reliable on this hardware, skipped\n");
        return EOK;
    }

    if (!s_tsc_ctx.hz)
        tsc_try_detect_frequency_from_cpuid();

    if (!s_tsc_ctx.hz)
        early_tsc_calibrate();

    if (likely(s_tsc_ctx.hz))
        tsc_clocksource_register();

    return EOK;
}
INIT_CALL_AT(X86_EARLY_TSC_SETUP, x86_early_tsc_setup);
