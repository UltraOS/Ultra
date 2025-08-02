#include <boot/ultra_protocol.h>
#include <boot/boot.h>

#include <arch/private/descriptors.h>
#include <arch/private/idt.h>

static descriptor_t g_gdt[NUM_GDT_ENTRIES] = {
    [DESC_IDX(KERNEL_CS)] = SEGMENT_KERNEL_CODE64,
    [DESC_IDX(KERNEL_SS)] = SEGMENT_KERNEL_DATA64,
    [DESC_IDX(USER_CS_COMPAT)] = SEGMENT_USER_CODE32,
    [DESC_IDX(USER_SS)] = SEGMENT_USER_DATA64,
    [DESC_IDX(USER_CS)] = SEGMENT_USER_CODE64,
};

void arch_init_early(void)
{
    struct descriptor_ptr gdt_ptr = {
        .limit = sizeof(g_gdt) - 1,
        .base = (ptr_t)g_gdt,
    };
    load_gdt(&gdt_ptr);

    idt_init();
}

void x86_entry(struct ultra_boot_context *ctx, uint32_t magic)
{
    if (magic != ULTRA_MAGIC)
        for (;;);

    entry(ctx);
}
