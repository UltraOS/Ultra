#include <arch/private/asm_registers.h>
#include <arch/private/unwind_hints.h>
#include <arch/private/asm_helpers.h>
#include <arch/private/idt.h>

ASM_PRELUDE

.macro PUSH_GP_REGS preserve_ret_addr=0
    .if \preserve_ret_addr
        // Push the second register first
        PUSH_REG_WITH_UNWIND_HINT(rsi)

        // Then overwrite it with the return value
        mov rsi, [rsp + ULTRA_ARCH_WIDTH]

        // Finally, overwrite the return value with the first register
        mov [rsp + ULTRA_ARCH_WIDTH], rdi
    .else
        PUSH_WITH_UNWIND_HINT(rdi)
        PUSH_WITH_UNWIND_HINT(rsi)
    .endif

    PUSH_WITH_UNWIND_HINT(rdx)
    PUSH_WITH_UNWIND_HINT(rcx)
    PUSH_WITH_UNWIND_HINT(rax)
    PUSH_WITH_UNWIND_HINT(r8)
    PUSH_WITH_UNWIND_HINT(r9)
    PUSH_WITH_UNWIND_HINT(r10)
    PUSH_WITH_UNWIND_HINT(r11)
    PUSH_WITH_UNWIND_HINT(rbx)
    PUSH_WITH_UNWIND_HINT(rbp)
    PUSH_WITH_UNWIND_HINT(r12)
    PUSH_WITH_UNWIND_HINT(r13)
    PUSH_WITH_UNWIND_HINT(r14)
    PUSH_WITH_UNWIND_HINT(r15)

    .if \preserve_ret_addr
        PUSH_WITH_UNWIND_HINT(rsi)
    .endif
.endm

.macro POP_GP_REGS
    POP_REG_WITH_UNWIND_HINT(r15)
    POP_REG_WITH_UNWIND_HINT(r14)
    POP_REG_WITH_UNWIND_HINT(r13)
    POP_REG_WITH_UNWIND_HINT(r12)
    POP_REG_WITH_UNWIND_HINT(rbp)
    POP_REG_WITH_UNWIND_HINT(rbx)
    POP_REG_WITH_UNWIND_HINT(r11)
    POP_REG_WITH_UNWIND_HINT(r10)
    POP_REG_WITH_UNWIND_HINT(r9)
    POP_REG_WITH_UNWIND_HINT(r8)
    POP_REG_WITH_UNWIND_HINT(rax)
    POP_REG_WITH_UNWIND_HINT(rcx)
    POP_REG_WITH_UNWIND_HINT(rdx)
    POP_REG_WITH_UNWIND_HINT(rsi)
    POP_REG_WITH_UNWIND_HINT(rdi)
.endm

.macro UNWIND_HINT_GP_REGS base_offset=0, adjust_cfa=1
    .if \adjust_cfa
        UNWIND_HINT_ADJUST_CFA(GP_REGS_END_OFFSET)
    .endif

    UNWIND_HINT_REG_OFFSET(r15, R15_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(r14, R14_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(r13, R13_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(r12, R12_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(rbp, RBP_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(rbx, RBX_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(r11, R11_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(r10, R10_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(r9, R9_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(r8, R8_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(rax, RAX_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(rcx, RCX_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(rdx, RDX_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(rsi, RSI_OFFSET + \base_offset)
    UNWIND_HINT_REG_OFFSET(rdi, RDI_OFFSET + \base_offset)
.endm

ASM_LOCAL_FUNCTION interrupt_begin, unwind_hint=0
    /*
     * We assume the error code/vector value is already on the stack.
     * There's also a return value so account for that as well.
     */
    UNWIND_HINT_INTERRUPT_FRAME extra_offset=ULTRA_ARCH_WIDTH*2
    cld

    PUSH_GP_REGS preserve_ret_addr=1
    UNWIND_HINT_GP_REGS base_offset=ULTRA_ARCH_WIDTH, adjust_cfa=0
    ret
ASM_FUNCTION_END interrupt_begin

ASM_LOCAL_FUNCTION interrupt_end, unwind_hint=0
    UNWIND_HINT_INTERRUPT_FRAME extra_offset=IRETQ_FRAME_OFFSET
    UNWIND_HINT_GP_REGS adjust_cfa=0

    POP_GP_REGS

    // Skip AUX value
    add rsp, ULTRA_ARCH_WIDTH
    UNWIND_HINT_AFTER_POP

    iretq
ASM_FUNCTION_END interrupt_end

ASM_LOCAL_FUNCTION X86_IRQ_DISPATCH_ASM, unwind_hint=0
    UNWIND_HINT_INTERRUPT_FRAME extra_offset=ULTRA_ARCH_WIDTH
    call interrupt_begin
    UNWIND_HINT_GP_REGS

    mov rdi, rsp
    .extern X86_IRQ_DISPATCH
    call X86_IRQ_DISPATCH

    jmp interrupt_end
ASM_FUNCTION_END X86_IRQ_DISPATCH_ASM

/*
 * Prevent the assembler from generating an imm32 push for vector > 127 in an
 * attempt to avoid a sign extended push. We don't care and would like a smaller
 * encoding instead.
 */
#define PUSH_IMM8 0x6A

.macro MAKE_IDT_THUNK name:req, func:req, aux_value, needs_aux=0
.pushsection ".text.idt_thunks"
ASM_LOCAL_FUNCTION \name, unwind_hint=0, align=IDT_THUNK_SIZE
    .if \needs_aux
        UNWIND_HINT_INTERRUPT_FRAME

        .byte PUSH_IMM8, \aux_value
        UNWIND_HINT_AFTER_PUSH
    .else
        // We assume an AUX value is already pushed by hardware
        UNWIND_HINT_INTERRUPT_FRAME extra_offset=ULTRA_ARCH_WIDTH
    .endif

    jmp \func

    UNWIND_HINT_END_OF_FUNCTION

/*
 * Pad the thunk with int3 up to the desired size, this also ensures it's not
 * larger than it's supposed to be.
 */
.fill \name + IDT_THUNK_SIZE - ., 1, 0xCC
.size \name, IDT_THUNK_SIZE
.popsection
.endm

.macro MAKE_EXCEPTION_TRAMPOLINE name
ASM_LOCAL_FUNCTION handle_\name\()_asm, unwind_hint=0
    UNWIND_HINT_INTERRUPT_FRAME extra_offset=8

    call interrupt_begin
    UNWIND_HINT_GP_REGS

    mov rdi, rsp
    call handle_\name
    jmp interrupt_end
ASM_FUNCTION_END handle_\name\()_asm
.endm

.macro MAKE_EXCEPTION_HANDLER name, pushes_error_code=0
    MAKE_EXCEPTION_TRAMPOLINE \name

    .if \pushes_error_code
        MAKE_IDT_THUNK \name\()_thunk, handle_\name\()_asm
    .else
        MAKE_IDT_THUNK \name\()_thunk, handle_\name\()_asm, 0xFF, needs_aux=1
    .endif

.endm

.macro MAKE_IRQ_THUNK irq_num:req
     MAKE_IDT_THUNK irq_\irq_num\()_thunk, X86_IRQ_DISPATCH_ASM, \irq_num, \
                    needs_aux=1
.endm

.macro MAKE_RESERVED_EXCEPTION_THUNK number:req
    MAKE_IDT_THUNK reserved_\number\()_thunk, X86_EXCEPTION_RSVD_ASM, \number, \
                   needs_aux=1
.endm

MAKE_EXCEPTION_TRAMPOLINE X86_EXCEPTION_RSVD

MAKE_EXCEPTION_HANDLER X86_EXCEPTION_DE
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_DB
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_NMI
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_BP
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_OF
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_BR
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_UD
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_NM
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_DF, pushes_error_code=1
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_CSO
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_TS, pushes_error_code=1
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_NP, pushes_error_code=1
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_SS, pushes_error_code=1
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_GP, pushes_error_code=1
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_PF, pushes_error_code=1
MAKE_RESERVED_EXCEPTION_THUNK 15
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_MF
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_AC, pushes_error_code=1
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_MC
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_XM
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_VE
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_CP, pushes_error_code=1
MAKE_RESERVED_EXCEPTION_THUNK 22
MAKE_RESERVED_EXCEPTION_THUNK 23
MAKE_RESERVED_EXCEPTION_THUNK 24
MAKE_RESERVED_EXCEPTION_THUNK 25
MAKE_RESERVED_EXCEPTION_THUNK 26
MAKE_RESERVED_EXCEPTION_THUNK 27
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_HV
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_VC, pushes_error_code=1
MAKE_EXCEPTION_HANDLER X86_EXCEPTION_SX, pushes_error_code=1
MAKE_RESERVED_EXCEPTION_THUNK 31

#ifdef __clang__
.altmacro

.set irq_number, 0
.rept NUM_IDT_ENTRIES - NUM_X86_EXCEPTIONS
    MAKE_IRQ_THUNK %irq_number
    .set irq_number, irq_number + 1
.endr

#else

.rept NUM_IDT_ENTRIES - NUM_X86_EXCEPTIONS
    MAKE_IRQ_THUNK \+
.endr

#endif
