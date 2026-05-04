#include <private/arch/unwind.h>
#include <arch/private/unwind.h>

#include <symbols.h>
#include <unwind.h>
#include <unsafe_access.h>

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

error_t arch_try_recover_previous_frame(struct unwind_state *state) {
    error_t ret;
    reg_t prev_pc;
    reg_t *rsp = (reg_t*)state->frame[DWARF_REG_RSP];

    /*
     * The address of our caller should be right there, at the current RSP.
     * If this was indeed a bogus function pointer call, we should be able
     * to recover it no problem.
     */
    ret = try_memcpy(&prev_pc, rsp, sizeof(reg_t));
    if (ret != EOK)
        return ret;

    if (!address_is_kernel_code(prev_pc))
        /*
         * Yeah, no.. There's a bigger stack corruption going on.
         * Whatever we have recovered from our stack is a bogus value as well.
         */
        return ENOENT;

    /*
     * Success, now fix up the frame and attempt to fast forward it to
     * the after-the-call state. Basically emulate a 'ret' instruction
     * that pops the RIP off the stack and jumps to it.
     */
    state->signal_frame = false;
    state->ret_reg_idx = ARCH_DWARF_PC_REG;
    state->frame[state->ret_reg_idx] = prev_pc;
    state->frame[DWARF_REG_RSP] += sizeof(reg_t);

    return EOK;
}
