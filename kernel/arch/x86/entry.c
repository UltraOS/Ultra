#include <boot/ultra_protocol.h>
#include <boot/boot.h>

#include <arch/private/descriptors.h>
#include <arch/private/idt.h>
#include <arch/private/cpu.h>
#include <arch/private/msr.h>

#include <private/arch/init.h>

#include <per_cpu.h>
#include <free_after_init.h>

DEFINE_PER_CPU(descriptor_t [NUM_GDT_ENTRIES], g_this_cpu_gdt) = {
    [DESC_IDX(KERNEL_CS)] = SEGMENT_KERNEL_CODE64,
    [DESC_IDX(KERNEL_SS)] = SEGMENT_KERNEL_DATA64,
    [DESC_IDX(USER_CS_COMPAT)] = SEGMENT_USER_CODE32,
    [DESC_IDX(USER_SS)] = SEGMENT_USER_DATA64,
    [DESC_IDX(USER_CS)] = SEGMENT_USER_CODE64,
};

void INIT_CODE arch_init_early(void)
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

    pr_info(
        "Running on %s (%d:%d:%d)\n",
        g_cpu_info.name, g_cpu_info.family, g_cpu_info.model,
        g_cpu_info.stepping
    );
}

ULTRA_ENTRYPOINT(x86)
{
    if (magic != ULTRA_MAGIC)
        for (;;);

    entry(ctx);
}
