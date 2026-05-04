#include <asm_helpers.h>
#include <unwind_hints.h>
#include <linker.h>

.extern arm_entry

.pushsection SECTION_NAME(FREE_AFTER_INIT_TEXT_SECTION), "ax", @progbits

ASM_GLOBAL_FUNCTION arm_entry_asm
    UNWIND_HINT_UNDEFINED(lr)
    bl arm_entry
ASM_FUNCTION_END arm_entry_asm

.popsection
