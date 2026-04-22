#include <common/helpers.h>
#include <common/types.h>

#include <log.h>
#include <panic.h>
#include <unsafe_access.h>

#include <arch/private/idt.h>
#include <arch/registers.h>
#include <arch/private/asm_registers.h>
#include <arch/private/abortable_instructions.h>

#include <private/arch/abortable_instructions.h>

// Ensure both ASM and C code have the same idea about register layout
BUILD_BUG_ON(R15_OFFSET != offsetof(struct registers, r15));
BUILD_BUG_ON(R14_OFFSET != offsetof(struct registers, r14));
BUILD_BUG_ON(R13_OFFSET != offsetof(struct registers, r13));
BUILD_BUG_ON(R12_OFFSET != offsetof(struct registers, r12));
BUILD_BUG_ON(RBP_OFFSET != offsetof(struct registers, rbp));
BUILD_BUG_ON(RBX_OFFSET != offsetof(struct registers, rbx));
BUILD_BUG_ON(R11_OFFSET != offsetof(struct registers, r11));
BUILD_BUG_ON(R10_OFFSET != offsetof(struct registers, r10));
BUILD_BUG_ON(R9_OFFSET  != offsetof(struct registers, r9));
BUILD_BUG_ON(R8_OFFSET  != offsetof(struct registers, r8));
BUILD_BUG_ON(RAX_OFFSET != offsetof(struct registers, rax));
BUILD_BUG_ON(RCX_OFFSET != offsetof(struct registers, rcx));
BUILD_BUG_ON(RDX_OFFSET != offsetof(struct registers, rdx));
BUILD_BUG_ON(RSI_OFFSET != offsetof(struct registers, rsi));
BUILD_BUG_ON(RDI_OFFSET != offsetof(struct registers, rdi));
BUILD_BUG_ON(AUX_OFFSET != offsetof(struct registers, error_code));
BUILD_BUG_ON(RIP_OFFSET != offsetof(struct registers, rip));
BUILD_BUG_ON(CS_OFFSET  != offsetof(struct registers, cs));
BUILD_BUG_ON(FLAGS_OFFSET != offsetof(struct registers, flags));
BUILD_BUG_ON(RSP_OFFSET != offsetof(struct registers, rsp));
BUILD_BUG_ON(SS_OFFSET != offsetof(struct registers, ss));

#define STUB_EXCEPTION(x)                               \
    EXCEPTION_HANDLER(x)                                \
    {                                                   \
        UNREFERENCED_PARAMETER(regs);                   \
        panic("Unexpected exception: %s\n", TO_STR(x)); \
    }

STUB_EXCEPTION(X86_EXCEPTION_DE)
STUB_EXCEPTION(X86_EXCEPTION_DB)
STUB_EXCEPTION(X86_EXCEPTION_NMI)
STUB_EXCEPTION(X86_EXCEPTION_BP)
STUB_EXCEPTION(X86_EXCEPTION_OF)
STUB_EXCEPTION(X86_EXCEPTION_BR)
STUB_EXCEPTION(X86_EXCEPTION_UD)
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

EXCEPTION_HANDLER(X86_EXCEPTION_PF)
{
    phys_addr_t addr;

    if (handle_abortable_instruction(regs))
        return;

    asm volatile("mov %%cr2, %0" : "=r"(addr));
    panic("Page fault at 0x%016llX\n", addr);
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

    panic("Unhandled #GP at 0x%016llX\n", regs->rip);
}
