#pragma once

#include <arch/private/asm_registers.h>

#define UNWIND_HINT_START_OF_FUNCTION .cfi_startproc
#define UNWIND_HINT_END_OF_FUNCTION .cfi_endproc
#define UNWIND_HINT_ADJUST_CFA(x) .cfi_adjust_cfa_offset x
#define UNWIND_HINT_AFTER_PUSH UNWIND_HINT_ADJUST_CFA(ULTRA_ARCH_WIDTH)
#define UNWIND_HINT_AFTER_POP UNWIND_HINT_ADJUST_CFA(-ULTRA_ARCH_WIDTH)
#define UNWIND_HINT_REG_OFFSET(reg, offset) .cfi_rel_offset reg, offset
#define UNWIND_HINT_REG_RESTORED(reg) .cfi_restore reg
#define UNWIND_HINT_UNDEFINED(reg) .cfi_undefined reg

.macro UNWIND_HINT_INTERRUPT_FRAME extra_offset=0
    UNWIND_HINT_START_OF_FUNCTION
    .cfi_signal_frame
    .cfi_def_cfa rsp, INTERRUPT_FRAME_SIZE + \extra_offset
    .cfi_rel_offset ss, INTERRUPT_FRAME_OFFSET(SS_OFFSET) + \extra_offset
    .cfi_rel_offset rsp, INTERRUPT_FRAME_OFFSET(RSP_OFFSET) + \extra_offset
    .cfi_rel_offset rflags, INTERRUPT_FRAME_OFFSET(FLAGS_OFFSET) + \extra_offset
    .cfi_rel_offset cs, INTERRUPT_FRAME_OFFSET(CS_OFFSET) + \extra_offset
    .cfi_rel_offset rip, INTERRUPT_FRAME_OFFSET(RIP_OFFSET) + \extra_offset
.endm

.macro UNWIND_HINT_AFTER_REG_PUSH reg
    UNWIND_HINT_REG_OFFSET(\reg, 0)
    UNWIND_HINT_AFTER_PUSH
.endm

.macro UNWIND_HINT_AFTER_REG_POP reg
    UNWIND_HINT_REG_RESTORED(\reg)
    UNWIND_HINT_AFTER_POP
.endm

#define PUSH_WITH_UNWIND_HINT(val) push val; UNWIND_HINT_AFTER_PUSH
#define PUSH_REG_WITH_UNWIND_HINT(reg) push reg; UNWIND_HINT_AFTER_REG_PUSH reg

#define POP_REG_WITH_UNWIND_HINT(reg) pop reg; UNWIND_HINT_AFTER_REG_POP reg
