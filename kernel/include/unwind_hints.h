#pragma once

#define UNWIND_HINT_START_OF_FUNCTION .cfi_startproc
#define UNWIND_HINT_END_OF_FUNCTION .cfi_endproc
#define UNWIND_HINT_ADJUST_CFA(x) .cfi_adjust_cfa_offset x
#define UNWIND_HINT_REG_OFFSET(reg, offset) .cfi_rel_offset reg, offset
#define UNWIND_HINT_REG_RESTORED(reg) .cfi_restore reg
#define UNWIND_HINT_UNDEFINED(reg) .cfi_undefined reg
