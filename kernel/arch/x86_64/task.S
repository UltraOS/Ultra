#include <arch/private/unwind_hints.h>
#include <arch/private/asm_helpers.h>

#include <generated/asm_offsets.h>

ASM_PRELUDE

.extern task_entry
.extern g_this_cpu_task

ASM_GLOBAL_FUNCTION arch_switch_to
    PUSH_REG_WITH_UNWIND_HINT(rbp)
    PUSH_REG_WITH_UNWIND_HINT(rbx)
    PUSH_REG_WITH_UNWIND_HINT(r12)
    PUSH_REG_WITH_UNWIND_HINT(r13)
    PUSH_REG_WITH_UNWIND_HINT(r14)
    PUSH_REG_WITH_UNWIND_HINT(r15)

    mov [rdi + TASK_ARCH_SP_OFFSET], rsp
    mov rsp, [rsi + TASK_ARCH_SP_OFFSET]
    mov gs:[rip + g_this_cpu_task], rsi

    POP_REG_WITH_UNWIND_HINT(r15)
    POP_REG_WITH_UNWIND_HINT(r14)
    POP_REG_WITH_UNWIND_HINT(r13)
    POP_REG_WITH_UNWIND_HINT(r12)
    POP_REG_WITH_UNWIND_HINT(rbx)
    POP_REG_WITH_UNWIND_HINT(rbp)

    mov rax, rdi
    ret
ASM_FUNCTION_END arch_switch_to

ASM_GLOBAL_FUNCTION x86_task_entry_asm
    UNWIND_HINT_UNDEFINED(rip)

    mov rdi, rax
    mov rsi, rbx
    mov rdx, r12
    call task_entry
ASM_FUNCTION_END x86_task_entry_asm
