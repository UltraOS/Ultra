#include <arch/private/idt.h>
#include <arch/private/descriptors.h>

#include <linker.h>

#include <common/types.h>

extern ptr_t LINKER_SYMBOL(idt_thunks)[];
BUILD_BUG_ON(IDT_THUNK_SIZE != sizeof(ptr_t));

struct interrupt_descriptor {
    descriptor_t low;
    descriptor_t high;
};
struct interrupt_descriptor g_idt[NUM_IDT_ENTRIES];

static void idt_load(void)
{
    struct descriptor_ptr idt_ptr = {
        .limit = sizeof(g_idt) - 1,
        .base = (ptr_t)g_idt,
    };

    asm volatile("lidt %0" :: "m"(idt_ptr));
}

void idt_init(void)
{
    size_t i;

    for (i = 0; i < NUM_IDT_ENTRIES; ++i) {
        ptr_t this_thunk = (ptr_t)&LINKER_SYMBOL(idt_thunks)[i];

        g_idt[i] = (struct interrupt_descriptor) {
            .low = INTERRUPT_DESCRIPTOR_LOW(this_thunk, 0, 0),
            .high = INTERRUPT_DESCRIPTOR_HIGH(this_thunk),
        };
    }

    idt_load();
}
