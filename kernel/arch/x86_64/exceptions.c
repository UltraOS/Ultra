#include <common/helpers.h>
#include <common/types.h>

#include <log.h>
#include <panic.h>
#include <unsafe_access.h>

#include <arch/private/idt.h>
#include <arch/registers.h>
#include <arch/private/abortable_instructions.h>

#include <private/arch/abortable_instructions.h>
#include <private/arch/bug.h>

#define STUB_EXCEPTION(x)                             \
    EXCEPTION_HANDLER(x)                              \
    {                                                 \
        UNREFERENCED_PARAMETER(regs);                 \
        panic("Unexpected exception: %s", TO_STR(x)); \
    }

STUB_EXCEPTION(X86_EXCEPTION_DE)
STUB_EXCEPTION(X86_EXCEPTION_DB)
STUB_EXCEPTION(X86_EXCEPTION_NMI)
STUB_EXCEPTION(X86_EXCEPTION_BP)
STUB_EXCEPTION(X86_EXCEPTION_OF)
STUB_EXCEPTION(X86_EXCEPTION_BR)
STUB_EXCEPTION(X86_EXCEPTION_NM)
STUB_EXCEPTION(X86_EXCEPTION_DF)
STUB_EXCEPTION(X86_EXCEPTION_CSO)
STUB_EXCEPTION(X86_EXCEPTION_TS)
STUB_EXCEPTION(X86_EXCEPTION_NP)
STUB_EXCEPTION(X86_EXCEPTION_SS)
STUB_EXCEPTION(X86_EXCEPTION_MF)
STUB_EXCEPTION(X86_EXCEPTION_AC)
STUB_EXCEPTION(X86_EXCEPTION_MC)
STUB_EXCEPTION(X86_EXCEPTION_XM)
STUB_EXCEPTION(X86_EXCEPTION_VE)
STUB_EXCEPTION(X86_EXCEPTION_CP)
STUB_EXCEPTION(X86_EXCEPTION_HV)
STUB_EXCEPTION(X86_EXCEPTION_VC)
STUB_EXCEPTION(X86_EXCEPTION_SX)
STUB_EXCEPTION(X86_EXCEPTION_RSVD)

EXCEPTION_HANDLER(X86_EXCEPTION_UD)
{
    if (bug_handle_trap(regs))
        return;

    if (handle_abortable_instruction(regs))
        return;

    panic("Invalid opcode at 0x%016llX", regs->rip);
}

EXCEPTION_HANDLER(X86_EXCEPTION_PF)
{
    virt_addr_t addr;

    if (handle_abortable_instruction(regs))
        return;

    asm volatile("mov %%cr2, %0" : "=r"(addr));
    panic("Page fault at 0x%016zX", addr);
}

bool arch_handle_abortable_instruction(
    struct registers *regs, const struct abortable_instruction *ai
)
{
    reg_t ret_rip;

    // Only MSR helpers have an arch-specific handler
    if (!(ai->arch_flags & (ABORTABLE_INSTRUCTION_RDMSR_UNSAFE |
                            ABORTABLE_INSTRUCTION_WRMSR_UNSAFE)))
        return false;

    // MSR helpers don't use the stack, we know where the return RIP is
    if (is_error(try_memcpy(&ret_rip, (char*)regs->rsp, sizeof(ret_rip))))
        ret_rip = 0;

    if (ai->arch_flags & ABORTABLE_INSTRUCTION_RDMSR_UNSAFE) {
        pr_warn(
            "Unchecked read from MSR 0x%08X failed at %pSM\n",
            (u32)regs->rcx, &ret_rip
        );
    } else {
        pr_warn(
            "Unchecked write of 0x%08X%08X to MSR 0x%08X failed at %pSM\n",
            (u32)regs->rdx, (u32)regs->rax, (u32)regs->rcx, &ret_rip
        );
    }

    // Leave exception handling up to generic code
    return false;
}

EXCEPTION_HANDLER(X86_EXCEPTION_GP)
{
    if (handle_abortable_instruction(regs))
        return;

    panic("Unhandled #GP at 0x%016llX", regs->rip);
}
