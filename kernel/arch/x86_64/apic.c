#define MSG_FMT(msg) "apic: " msg

#include <arch/private/msr.h>
#include <arch/private/cpu.h>
#include <arch/private/apic.h>

#include <bug.h>
#include <free_after_init.h>

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
