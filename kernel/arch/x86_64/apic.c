#define MSG_FMT(msg) "apic: " msg

#include <arch/private/msr.h>
#include <arch/private/cpu.h>
#include <arch/private/apic.h>
#include <arch/private/i8259a.h>
#include <arch/private/vectors.h>
#include <arch/private/idt.h>
#include <arch/registers.h>
#include <arch/smp.h>

#include <bug.h>
#include <free_after_init.h>
#include <init_level.h>

static UNUSED_DECL u64 s_freq_hz;

struct apic *g_apic = nullptr;
enum apic_mode g_apic_mode = APIC_MODE_NONE;

u32 g_bsp_apic_id = APIC_ID_NONE;
u32 g_boot_cpu_apic_id = APIC_ID_NONE;
u32 g_max_apic_id = 0xFF - 1;

extern struct apic g_xapic;
extern struct apic g_x2apic;

static INIT_RODATA struct apic *const s_apic_modes[APIC_NUM_MODES] = {
    [APIC_MODE_X] = &g_xapic,
    [APIC_MODE_X2] = &g_x2apic,
};

static INIT_RODATA const char *const s_apic_mode_strings[APIC_NUM_MODES] = {
    [APIC_MODE_NONE] = "None",
    [APIC_MODE_X] = "xAPIC",
    [APIC_MODE_X2] = "x2APIC",
};

void INIT_CODE apic_detect(void)
{
    u64 apic_base_msr;

    if (unlikely(!all_cpus_have(X86_FEATURE_APIC)))
        panic("APIC not supported or disabled in software\n");

    apic_base_msr = rdmsr_or_die(MSR_IA32_APIC_BASE);

    /*
     * If BIOS clears this bit, that also clears the CPUID APIC bit, so that
     * should be guaranteed set here, with the base address also being valid.
     */
    BUG_ON(!(apic_base_msr & IA32_APIC_BASE_IS_ENABLED));

    g_apic_mode = all_cpus_have(X86_FEATURE_X2APIC) ?
        APIC_MODE_X2 : APIC_MODE_X;
    g_apic = s_apic_modes[g_apic_mode];

    pr_info("using %s mode\n", s_apic_mode_strings[g_apic_mode]);
    g_apic->setup();

    g_boot_cpu_apic_id = g_apic->read(APIC_REG_ID);
    if (unlikely(!(apic_base_msr & IA32_APIC_BASE_IS_BSP))) {
        pr_warn("boot CPU (0x%08X) is not the BSP!\n", g_boot_cpu_apic_id);
        return;
    }

    g_bsp_apic_id = g_boot_cpu_apic_id;
}

void apic_set_known_frequency(u64 hz)
{
    s_freq_hz = hz;
}

void apic_eoi(void)
{
    g_apic->write(APIC_REG_EOI, 0);
}

#define APIC_REG_STRIDE 0x10

// The ISR/TMR/IRR banks are bitmaps split into 32-bit registers
#define APIC_BITS_PER_BITMAP_REG 32
#define APIC_NUM_BITMAP_REGS \
    (NUM_IDT_ENTRIES / APIC_BITS_PER_BITMAP_REG)

static enum apic_reg apic_bitmap_reg(enum apic_reg base, u32 index)
{
    return base + index * APIC_REG_STRIDE;
}

bool apic_vector_in_isr(u8 vector)
{
    u32 value;

    value = g_apic->read(
        apic_bitmap_reg(APIC_REG_ISR, vector / APIC_BITS_PER_BITMAP_REG)
    );
    return value & BIT_U32(vector % APIC_BITS_PER_BITMAP_REG);
}

FIXED_VECTOR_HANDLER(X86_APIC_SPURIOUS, VECTOR_APIC_SPURIOUS)
{
    UNREFERENCED_PARAMETER(regs);

    // A real spurious interrupt has no in-service bit, never EOI it
    pr_warn("spurious APIC interrupt on CPU%u\n", unstable_cpu_id());
}

FIXED_VECTOR_HANDLER(X86_APIC_ERROR, VECTOR_APIC_ERROR)
{
    u32 esr;

    UNREFERENCED_PARAMETER(regs);

    // A write is required to latch the current error state
    g_apic->write(APIC_REG_ESR, 0);
    esr = g_apic->read(APIC_REG_ESR);
    apic_eoi();

    pr_warn("error on CPU%u: 0x%02X\n", unstable_cpu_id(), esr);
}

static u32 INIT_CODE apic_count_in_service(void)
{
    u32 i, count = 0;

    for (i = 0; i < APIC_NUM_BITMAP_REGS; i++) {
        u32 value;

        value = g_apic->read(apic_bitmap_reg(APIC_REG_ISR, i));
        while (value) {
            value &= value - 1;
            count++;
        }
    }

    return count;
}

static void INIT_CODE apic_warn_on_stale_bits(
    enum apic_reg base, const char *name
)
{
    u32 i, value;

    for (i = 0; i < APIC_NUM_BITMAP_REGS; i++) {
        value = g_apic->read(apic_bitmap_reg(base, i));
        if (unlikely(value))
            pr_warn("stale %s%u bits: 0x%08X\n", name, i, value);
    }
}

/*
 * We don't know anything about the APIC state at handover from the bootloader
 * and/or firmware, so we drain whatever pending/in-service IRQs it might have
 * left here.
 */
static void INIT_CODE apic_drain_stale_state(void)
{
    u32 count;

    count = apic_count_in_service();
    while (count--)
        apic_eoi();

    apic_warn_on_stale_bits(APIC_REG_ISR, "ISR");
    apic_warn_on_stale_bits(APIC_REG_IRR, "IRR");
}

void INIT_CODE apic_cpu_init(void)
{
    u32 value;

    // Disable APIC while we initialize it, this also masks all LVTs
    value = g_apic->read(APIC_REG_SVR);
    g_apic->write(APIC_REG_SVR, value & ~APIC_SVR_ENABLE);

    /*
     * Block priority classes 0 and 1 so that a message with a vector
     * in the exception range can never be delivered as an interrupt
     * and mistaken for an exception.
     */
    g_apic->write(APIC_REG_TPR, 0x10);

    apic_drain_stale_state();

    /*
     * Enable before touching the LVTs, the mask bits cannot be
     * cleared while the APIC is software disabled.
     */
    g_apic->write(APIC_REG_SVR, APIC_SVR_ENABLE | VECTOR_APIC_SPURIOUS);

    g_apic->write(
        APIC_REG_LVT_LINT0, APIC_LVT_DELIVERY_EXTINT | APIC_LVT_MASKED
    );

    // Only the boot CPU listens to the external NMI line
    value = APIC_LVT_DELIVERY_NMI;
    if (unstable_cpu_id() != 0)
        value |= APIC_LVT_MASKED;
    g_apic->write(APIC_REG_LVT_LINT1, value);

    // Arm error reporting, a write latches the current error state
    g_apic->write(APIC_REG_LVT_ERROR, VECTOR_APIC_ERROR);
    g_apic->write(APIC_REG_ESR, 0);
    g_apic->read(APIC_REG_ESR);
}

static error_t INIT_CODE apic_bsp_init(void)
{
    i8259a_quiesce();
    apic_cpu_init();
    return EOK;
}
INIT_CALL_POST(X86_PLATFORM_INFO_AVAILABLE, apic_bsp_init);
