#pragma once

#include <common/helpers.h>

#define NUM_IDT_ENTRIES 256
#define NUM_X86_EXCEPTIONS 32

#define IDT_THUNK_SIZE 8

#define X86_EXCEPTION_DE division_error
#define X86_EXCEPTION_DB debug
#define X86_EXCEPTION_NMI nmi
#define X86_EXCEPTION_BP breakpoint
#define X86_EXCEPTION_OF overflow
#define X86_EXCEPTION_BR bound_range_exceeded
#define X86_EXCEPTION_UD invalid_opcode
#define X86_EXCEPTION_NM device_not_available
#define X86_EXCEPTION_DF double_fault
#define X86_EXCEPTION_CSO coprocessor_segment_overrun
#define X86_EXCEPTION_TS invalid_tss
#define X86_EXCEPTION_NP segment_not_preset
#define X86_EXCEPTION_SS stack_segment_fault
#define X86_EXCEPTION_GP general_protection_fault
#define X86_EXCEPTION_PF page_fault
#define X86_EXCEPTION_MF fpu_exception
#define X86_EXCEPTION_AC alignment_check
#define X86_EXCEPTION_MC mce
#define X86_EXCEPTION_XM simd_fpu_exception
#define X86_EXCEPTION_VE virtualization_exception
#define X86_EXCEPTION_CP control_protection
#define X86_EXCEPTION_HV hypervisor_injection_exception
#define X86_EXCEPTION_VC vmm_communication_exception
#define X86_EXCEPTION_SX security_exception
#define X86_EXCEPTION_RSVD reserved_exception

#define X86_IRQ_DISPATCH irq_dispatch

#define X86_IRQ_DISPATCH_ASM CONCAT(X86_IRQ_DISPATCH, _asm)
#define X86_EXCEPTION_RSVD_ASM CONCAT(handle_, CONCAT(X86_EXCEPTION_RSVD, _asm))

#ifndef __ASSEMBLER__

#define EXCEPTION_HANDLER(x) void CONCAT(handle_, x)(struct registers *regs)
#define IRQ_HANDLER void X86_IRQ_DISPATCH(struct registers *regs)

void idt_init(void);

#endif
