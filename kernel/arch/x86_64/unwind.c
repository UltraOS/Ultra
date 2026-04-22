#include <private/arch/unwind.h>
#include <arch/private/unwind.h>
#include <unwind.h>

void arch_registers_to_dwarf_registers(
    struct registers *regs, ptr_t *dwarf_regs
)
{
    dwarf_regs[DWARF_REG_RAX] = regs->rax;
    dwarf_regs[DWARF_REG_RDX] = regs->rdx;
    dwarf_regs[DWARF_REG_RCX] = regs->rcx;
    dwarf_regs[DWARF_REG_RBX] = regs->rbx;
    dwarf_regs[DWARF_REG_RSI] = regs->rsi;
    dwarf_regs[DWARF_REG_RDI] = regs->rdi;
    dwarf_regs[DWARF_REG_RBP] = regs->rbp;
    dwarf_regs[DWARF_REG_RSP] = regs->rsp;
    dwarf_regs[DWARF_REG_R8] = regs->r8;
    dwarf_regs[DWARF_REG_R9] = regs->r9;
    dwarf_regs[DWARF_REG_R10] = regs->r10;
    dwarf_regs[DWARF_REG_R11] = regs->r11;
    dwarf_regs[DWARF_REG_R12] = regs->r12;
    dwarf_regs[DWARF_REG_R13] = regs->r13;
    dwarf_regs[DWARF_REG_R14] = regs->r14;
    dwarf_regs[DWARF_REG_R15] = regs->r15;
    dwarf_regs[DWARF_REG_RIP] = regs->rip;
}
