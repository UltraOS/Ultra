#include <private/arch/asm_offsets.h>

#include <arch/registers.h>

void asm_offsets(void)
{
    ASM_OFFSET(R15_OFFSET, struct registers, r15);
    ASM_OFFSET(R14_OFFSET, struct registers, r14);
    ASM_OFFSET(R13_OFFSET, struct registers, r13);
    ASM_OFFSET(R12_OFFSET, struct registers, r12);
    ASM_OFFSET(RBP_OFFSET, struct registers, rbp);
    ASM_OFFSET(RBX_OFFSET, struct registers, rbx);

    ASM_OFFSET(R11_OFFSET, struct registers, r11);
    ASM_OFFSET(R10_OFFSET, struct registers, r10);
    ASM_OFFSET(R9_OFFSET, struct registers, r9);
    ASM_OFFSET(R8_OFFSET, struct registers, r8);
    ASM_OFFSET(RAX_OFFSET, struct registers, rax);
    ASM_OFFSET(RCX_OFFSET, struct registers, rcx);
    ASM_OFFSET(RDX_OFFSET, struct registers, rdx);
    ASM_OFFSET(RSI_OFFSET, struct registers, rsi);
    ASM_OFFSET(RDI_OFFSET, struct registers, rdi);

    ASM_OFFSET(AUX_OFFSET, struct registers, error_code);

    ASM_OFFSET(RIP_OFFSET, struct registers, rip);
    ASM_OFFSET(CS_OFFSET, struct registers, cs);
    ASM_OFFSET(FLAGS_OFFSET, struct registers, flags);
    ASM_OFFSET(RSP_OFFSET, struct registers, rsp);
    ASM_OFFSET(SS_OFFSET, struct registers, ss);

    ASM_CONSTANT(REGISTERS_SIZE, sizeof(struct registers));
}
