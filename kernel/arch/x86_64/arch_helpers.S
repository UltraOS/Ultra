#include <arch/private/descriptors.h>
#include <arch/private/unwind_hints.h>
#include <arch/private/asm_helpers.h>
#include <arch/private/abortable_instructions.h>

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

// error_t rdmsr(u32 msr, u64 *out_value);
ASM_GLOBAL_FUNCTION rdmsr
    mov ecx, edi

    ABORTABLE_INSTRUCTION(rdmsr)
    mov [rsi], eax
    mov [rsi + 4], edx
    xor eax, eax

    ABORTABLE_INSTRUCTION_RESUME_LABEL \
        flags=ABORTABLE_INSTRUCTION_ENOSYS_ON_ERROR
    ret
ASM_FUNCTION_END rdmsr

// void rdmsr_unsafe(u32 msr, u64 *out_value);
ASM_GLOBAL_FUNCTION rdmsr_unsafe
    mov ecx, edi

    ABORTABLE_INSTRUCTION(rdmsr)
    mov [rsi], eax
    mov [rsi + 4], edx

    ABORTABLE_INSTRUCTION_RESUME_LABEL                \
        flags=ABORTABLE_INSTRUCTION_HAS_ARCH_HANDLER, \
        x86_flags=ABORTABLE_INSTRUCTION_RDMSR_UNSAFE

    ret
ASM_FUNCTION_END rdmsr_unsafe

// error_t wrmsr(u32 msr, u64 value);
ASM_GLOBAL_FUNCTION wrmsr
    mov ecx, edi
    mov eax, esi
    mov rdx, rsi
    shr rdx, 32

    ABORTABLE_INSTRUCTION(wrmsr)
    xor eax, eax

    ABORTABLE_INSTRUCTION_RESUME_LABEL \
        flags=ABORTABLE_INSTRUCTION_ENOSYS_ON_ERROR

    ret
ASM_FUNCTION_END wrmsr

// void wrmsr_unsafe(u32 msr, u64 value);
ASM_GLOBAL_FUNCTION wrmsr_unsafe
    mov ecx, edi
    mov eax, esi
    mov rdx, rsi
    shr rdx, 32

    ABORTABLE_INSTRUCTION(wrmsr)
    ABORTABLE_INSTRUCTION_RESUME_LABEL                \
        flags=ABORTABLE_INSTRUCTION_HAS_ARCH_HANDLER, \
        x86_flags=ABORTABLE_INSTRUCTION_WRMSR_UNSAFE

    ret
ASM_FUNCTION_END wrmsr_unsafe
