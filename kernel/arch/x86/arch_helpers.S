#include <arch/private/descriptors.h>
#include <arch/private/unwind_hints.h>
#include <arch/private/asm_helpers.h>

ASM_PRELUDE

// void load_gdt(struct descriptor_ptr *ptr)
ASM_GLOBAL_FUNCTION load_gdt
    lgdt [rdi]
    lea rdi, [rip + 1f]

    PUSH_WITH_UNWIND_HINT(KERNEL_CS)
    PUSH_WITH_UNWIND_HINT(rdi)

    retfq
    UNWIND_HINT_ADJUST_CFA(-ULTRA_ARCH_WIDTH * 2)
1:
    xor eax, eax

    mov ds, eax
    mov fs, eax
    mov gs, eax
    mov ss, eax

    // Clear the LDT as well for good measure
    lldt ax

    ret
ASM_FUNCTION_END load_gdt
