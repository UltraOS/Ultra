#include <arch/private/unwind.h>
#include <arch/private/asm_helpers.h>

ASM_PRELUDE

.extern unwind_current_begin

ASM_GLOBAL_FUNCTION arch_unwind_current_begin
    mov [rdi + 8 * DWARF_REG_RAX], rax
    mov [rdi + 8 * DWARF_REG_RDX], rdx
    mov [rdi + 8 * DWARF_REG_RCX], rcx
    mov [rdi + 8 * DWARF_REG_RBX], rbx
    mov [rdi + 8 * DWARF_REG_RSI], rsi
    mov [rdi + 8 * DWARF_REG_RDI], rdi
    mov [rdi + 8 * DWARF_REG_RBP], rbp
    mov [rdi + 8 * DWARF_REG_RSP], rsp
    add qword ptr [rdi + 8 * DWARF_REG_RSP], 8
    mov [rdi + 8 * DWARF_REG_R8], r8
    mov [rdi + 8 * DWARF_REG_R9], r9
    mov [rdi + 8 * DWARF_REG_R10], r10
    mov [rdi + 8 * DWARF_REG_R11], r11
    mov [rdi + 8 * DWARF_REG_R12], r12
    mov [rdi + 8 * DWARF_REG_R13], r13
    mov [rdi + 8 * DWARF_REG_R14], r14
    mov [rdi + 8 * DWARF_REG_R15], r15

    mov r15, [rsp]
    mov [rdi + 8 * DWARF_REG_RIP], r15
    mov r15, [rdi + 8 * DWARF_REG_R15]

    jmp unwind_current_begin
ASM_FUNCTION_END arch_unwind_current_begin
