#define MSG_FMT(msg) "apic-timer: " msg

#include <arch/private/apic.h>
#include <arch/private/apic_timer.h>
#include <arch/private/vectors.h>
#include <arch/private/idt.h>
#include <arch/registers.h>
#include <arch/cpu_helpers.h>

#include <time/event_device.h>
#include <time/time.h>
#include <time/units.h>
#include <time/counter_device.h>

#include <log.h>
#include <init_level.h>
#include <free_after_init.h>

#define CALIBRATE_NS (20 * NS_PER_MS)

#define TIMER_DIVISOR 2
#define TIMER_DIVIDE_CONFIG CONCAT(APIC_TIMER_DIVIDE_BY_, TIMER_DIVISOR)

static u64 s_freq_hz;

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

FIXED_VECTOR_HANDLER(X86_APIC_TIMER, VECTOR_APIC_TIMER)
{
    UNREFERENCED_PARAMETER(regs);

    apic_eoi();
    event_device_interrupt(&s_apic_timer);
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

static error_t INIT_CODE apic_timer_init(void)
{
    const char *source = "known";
    struct human_repr freq;

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

    return event_device_register(&s_apic_timer, s_freq_hz / TIMER_DIVISOR);
}
INIT_CALL_POST(IRQS_AVAILABLE, apic_timer_init);
