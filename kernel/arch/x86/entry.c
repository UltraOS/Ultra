#include <boot/ultra_protocol.h>
#include <boot/boot.h>

#include <arch/private/descriptors.h>
#include <arch/private/idt.h>
#include <arch/private/cpu.h>

#include <private/arch/init.h>

#include <free_after_init.h>

static descriptor_t g_gdt[NUM_GDT_ENTRIES] = {
    [DESC_IDX(KERNEL_CS)] = SEGMENT_KERNEL_CODE64,
    [DESC_IDX(KERNEL_SS)] = SEGMENT_KERNEL_DATA64,
    [DESC_IDX(USER_CS_COMPAT)] = SEGMENT_USER_CODE32,
    [DESC_IDX(USER_SS)] = SEGMENT_USER_DATA64,
    [DESC_IDX(USER_CS)] = SEGMENT_USER_CODE64,
};

void INIT_CODE arch_init_early(void)
{
    struct descriptor_ptr gdt_ptr = {
        .limit = sizeof(g_gdt) - 1,
        .base = (ptr_t)g_gdt,
    };
    load_gdt(&gdt_ptr);

    idt_init();

    cpu_info_setup(&g_cpu_info);

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
