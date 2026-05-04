#include <arch/private/asm_helpers.h>

#include <unwind_hints.h>
#include <linker.h>

ASM_PRELUDE

.extern x86_entry

.pushsection SECTION_NAME(FREE_AFTER_INIT_TEXT_SECTION), "ax", @progbits

ASM_GLOBAL_FUNCTION x86_entry_asm
    UNWIND_HINT_UNDEFINED(rip)
    call x86_entry
ASM_FUNCTION_END x86_entry_asm

.popsection
