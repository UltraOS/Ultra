#include <boot/ultra_protocol.h>
#include <boot/boot.h>

#include <arch/private/descriptors.h>
#include <arch/private/idt.h>
#include <arch/private/cpu.h>
#include <arch/private/cr.h>
#include <arch/private/msr.h>
#include <arch/private/smp.h>
#include <arch/private/apic.h>
#include <arch/private/hypervisor.h>

#include <init_level.h>
#include <per_cpu.h>
#include <free_after_init.h>

DEFINE_PER_CPU(descriptor_t [NUM_GDT_ENTRIES], g_this_cpu_gdt) = {
    [DESC_IDX(KERNEL_CS)] = SEGMENT_KERNEL_CODE64,
    [DESC_IDX(KERNEL_SS)] = SEGMENT_KERNEL_DATA64,
    [DESC_IDX(USER_CS_COMPAT)] = SEGMENT_USER_CODE32,
    [DESC_IDX(USER_SS)] = SEGMENT_USER_DATA64,
    [DESC_IDX(USER_CS)] = SEGMENT_USER_CODE64,
};

static error_t INIT_CODE x86_early_init(void)
{
    struct descriptor_ptr gdt_ptr = {
        .limit = sizeof(g_this_cpu_gdt) - 1,
        .base = (ptr_t)g_this_cpu_gdt,
    };
    load_gdt(&gdt_ptr);

    idt_init();

    cpu_info_setup(&g_cpu_info);

    // Enable early per-cpu variables for the BSP
    wrmsr_or_die(MSR_GS_BASE, 0);

    cr0_setup();

    pr_info(
        "Running on %s (%d:%d:%d)\n",
        g_cpu_info.name_string, g_cpu_info.family, g_cpu_info.model,
        g_cpu_info.stepping
    );

    return EOK;
}
INIT_CALL_PRE(PRE_BOOT, x86_early_init);

bool g_can_skip_pio_delay = false;

static error_t INIT_CODE x86_platform_init(void)
{
    apic_detect();
    setup_smp_topology();

    x86_detect_hypervisor();
    if (hypervisor_supports(HYPERVISOR_FEATURE_SKIP_PORT_IO_DELAY))
        g_can_skip_pio_delay = true;

    return EOK;
}
INIT_CALL_POST(PLATFORM_INFO_AVAILABLE, x86_platform_init);

ULTRA_ENTRYPOINT(x86)
{
    if (magic != ULTRA_MAGIC)
        for (;;);

    entry(ctx);
}
