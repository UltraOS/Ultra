#define MSG_FMT(msg) "apic-timer: " msg

#include <arch/private/apic.h>
#include <arch/private/apic_timer.h>
#include <arch/private/cpu.h>
#include <arch/private/cpu_match.h>
#include <arch/private/hypervisor.h>
#include <arch/private/msr.h>
#include <arch/private/tsc.h>
#include <arch/private/vectors.h>
#include <arch/private/idt.h>
#include <arch/private/intel_family.h>
#include <arch/registers.h>
#include <arch/cpu_helpers.h>

#include <time/event_device.h>
#include <time/time.h>
#include <time/units.h>
#include <time/counter_device.h>

#include <common/overflow.h>

#include <log.h>
#include <init_level.h>
#include <free_after_init.h>

#define CALIBRATE_NS (20 * NS_PER_MS)

#define TIMER_DIVISOR 2
#define TIMER_DIVIDE_CONFIG CONCAT(APIC_TIMER_DIVIDE_BY_, TIMER_DIVISOR)

static u64 s_freq_hz;
static struct event_device *s_device;

void apic_set_known_frequency(u64 hz)
{
    s_freq_hz = hz;
}

static void apic_timer_arm(struct event_device *dev, u64 ticks)
{
    UNREFERENCED_PARAMETER(dev);

    g_apic->write(APIC_REG_TIMER_INITIAL_COUNT, ticks);
}

static void apic_timer_disarm(struct event_device *dev)
{
    UNREFERENCED_PARAMETER(dev);

    g_apic->write(APIC_REG_TIMER_INITIAL_COUNT, 0);
}

static struct event_device s_apic_timer = {
    .name = "apic-timer",
    .arm = apic_timer_arm,
    .disarm = apic_timer_disarm,

    // An initial count of 0 stops the timer
    .min_ticks = 1,
    .max_ticks = UNSIGNED_MAX(u32),
};

static void apic_deadline_arm(struct event_device *dev, u64 ticks)
{
    u64 tsc, deadline;

    UNREFERENCED_PARAMETER(dev);

    tsc = rdtsc();

    if (check_add_overflow(tsc, ticks, &deadline))
        deadline = UNSIGNED_MAX(u64);

    wrmsr_unsafe(MSR_IA32_TSC_DEADLINE, deadline);
}

static void apic_deadline_disarm(struct event_device *dev)
{
    UNREFERENCED_PARAMETER(dev);

    wrmsr_unsafe(MSR_IA32_TSC_DEADLINE, 0);
}

static struct event_device s_apic_deadline_timer = {
    .name = "apic-tsc-deadline",
    .arm = apic_deadline_arm,
    .disarm = apic_deadline_disarm,

    .min_ticks = 0,
    .max_ticks = UNSIGNED_MAX(u64),
};

FIXED_VECTOR_HANDLER(X86_APIC_TIMER, VECTOR_APIC_TIMER)
{
    UNREFERENCED_PARAMETER(regs);

    apic_eoi();
    event_device_interrupt(s_device);
}

static void INIT_CODE apic_timer_calibrate(void)
{
    u64 start_ns, elapsed_ns, ticks;

    if (current_counter_device() == nullptr)
        return;

    g_apic->write(APIC_REG_LVT_TIMER, VECTOR_APIC_TIMER | APIC_LVT_MASKED);
    g_apic->write(APIC_REG_TIMER_INITIAL_COUNT, UINT32_MAX);

    start_ns = ns_since_boot();
    do {
        arch_cpu_relax();
        elapsed_ns = ns_since_boot() - start_ns;
    } while (elapsed_ns < CALIBRATE_NS);

    ticks = UINT32_MAX - g_apic->read(APIC_REG_TIMER_CURRENT_COUNT);
    g_apic->write(APIC_REG_TIMER_INITIAL_COUNT, 0);

    s_freq_hz = (ticks * TIMER_DIVISOR * NS_PER_SEC) / elapsed_ns;
}

/*
 * These CPUs & microcode combinations apparently misbehave if the TSC_ADJUST
 * MSR is ever written (which even the firmware itself is known to do upon
 * leaving the SMM to hide stolen CPU cycles from the kernel).
 */
static INIT_RODATA const struct cpu_match_entry s_deadline_errata[] = {
    CPU_MATCH_VFM(INTEL_HASWELL, 0x22),
    CPU_MATCH_VFM_WITH_STEPPINGS(INTEL_HASWELL_SERVER, 0x2, 0x2, 0x3A),
    CPU_MATCH_VFM_WITH_STEPPINGS(INTEL_HASWELL_SERVER, 0x4, 0x4, 0x0F),
    CPU_MATCH_VFM(INTEL_HASWELL_MOBILE, 0x20),
    CPU_MATCH_VFM(INTEL_HASWELL_GFX, 0x17),

    CPU_MATCH_VFM(INTEL_BROADWELL, 0x25),
    CPU_MATCH_VFM(INTEL_BROADWELL_GFX, 0x17),
    CPU_MATCH_VFM(INTEL_BROADWELL_SERVER, 0x0B000020),
    CPU_MATCH_VFM_WITH_STEPPINGS(INTEL_BROADWELL_MSERVER, 0x2, 0x2, 0x00000011),
    CPU_MATCH_VFM_WITH_STEPPINGS(INTEL_BROADWELL_MSERVER, 0x3, 0x3, 0x0700000E),
    CPU_MATCH_VFM_WITH_STEPPINGS(INTEL_BROADWELL_MSERVER, 0x4, 0x4, 0x0F00000C),
    CPU_MATCH_VFM_WITH_STEPPINGS(INTEL_BROADWELL_MSERVER, 0x5, 0x5, 0x0E000003),

    CPU_MATCH_VFM(INTEL_SKYLAKE_MOBILE, 0xB2),
    CPU_MATCH_VFM_WITH_STEPPINGS(INTEL_SKYLAKE_SERVER, 0x3, 0x3, 0x01000136),
    CPU_MATCH_VFM_WITH_STEPPINGS(INTEL_SKYLAKE_SERVER, 0x4, 0x4, 0x02000014),
    CPU_MATCH_VFM(INTEL_SKYLAKE, 0xB2),

    CPU_MATCH_VFM(INTEL_KABYLAKE_MOBILE, 0x52),
    CPU_MATCH_VFM(INTEL_KABYLAKE, 0x52),
    {},
};

static bool INIT_CODE deadline_mode_is_broken(void)
{
    const struct cpu_match_entry *erratum;
    u32 revision;

    if (in_hypervisor())
        return false;

    erratum = cpu_match(s_deadline_errata);
    if (erratum == nullptr)
        return false;

    revision = g_cpu_info.microcode_revision;
    if (revision >= erratum->priv_data)
        return false;

    pr_warn(
        "TSC deadline mode is broken on microcode 0x%X, please update to "
        "at least 0x%zX\n", revision, erratum->priv_data
    );

    return true;
}

static bool INIT_CODE apic_timer_try_deadline_mode(void)
{
    u64 hz;

    if (!all_cpus_have(X86_FEATURE_TSC_DEADLINE))
        return false;

    hz = tsc_frequency();
    if (hz == 0)
        return false;

    if (deadline_mode_is_broken())
        return false;

    g_apic->write(
        APIC_REG_LVT_TIMER, VECTOR_APIC_TIMER | APIC_LVT_TIMER_TSC_DEADLINE
    );

    /*
     * Intel 64 and IA-32 Architectures Software Developer's Manual, section
     * TSC-Deadline Mode:
     *
     * "In xAPIC mode (in which the local-APIC registers are memory-mapped),
     * software must order the memory-mapped write to the LVT entry that enables
     * TSC-deadline mode and any subsequent WRMSR to the IA32_TSC_DEADLINE MSR.
     * Software can assure proper ordering by executing the MFENCE instruction
     * after the memory-mapped write and before any WRMSR. (In x2APIC mode, the
     * WRMSR instruction is used to write to the LVT entry. The processor
     * ensures the ordering of this write and any subsequent WRMSR to the
     * deadline; no fencing is required.)"
     */
    if (g_apic_mode == APIC_MODE_X)
        weak_wrmsr_fence();

    pr_info("using TSC deadline mode\n");
    s_device = &s_apic_deadline_timer;

    return event_device_register(s_device, hz) == EOK;
}

static error_t INIT_CODE apic_timer_init(void)
{
    const char *source = "known";
    struct human_repr freq;

    if (apic_timer_try_deadline_mode())
        return EOK;

    g_apic->write(APIC_REG_TIMER_DIVIDE_CONFIG, TIMER_DIVIDE_CONFIG);

    if (s_freq_hz == 0) {
        apic_timer_calibrate();
        source = "calibrated";
    }

    if (unlikely(s_freq_hz == 0)) {
        pr_warn("unable to calibrate!\n");
        return EOK;
    }

    frequency_to_human(s_freq_hz, &freq);
    pr_info(HUMAN_REPR_FMT " (%s)\n", HUMAN_REPR_ARG(freq), source);

    g_apic->write(APIC_REG_LVT_TIMER, VECTOR_APIC_TIMER);
    s_device = &s_apic_timer;

    return event_device_register(s_device, s_freq_hz / TIMER_DIVISOR);
}
INIT_CALL_POST(IRQS_AVAILABLE, apic_timer_init);
