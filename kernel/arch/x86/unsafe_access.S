#include <arch/private/asm_helpers.h>
#include <arch/private/abortable_instructions.h>

ASM_PRELUDE

// error_t try_memcpy(void *dst, const void *src, size_t n);
ASM_GLOBAL_FUNCTION try_memcpy
    mov rcx, rdx

    ABORTABLE_INSTRUCTION(rep movsb)
    xor eax, eax

    ABORTABLE_INSTRUCTION_RESUME_LABEL \
        flags=ABORTABLE_INSTRUCTION_EFAULT_ON_ERROR

    ret
ASM_FUNCTION_END try_memcpy
