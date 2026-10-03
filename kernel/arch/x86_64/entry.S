#include <arch/private/asm_helpers.h>
#include <arch/private/msr_definitions.h>

#include <unwind_hints.h>
#include <linker.h>

ASM_PRELUDE

.extern x86_entry

.pushsection SECTION_NAME(FREE_AFTER_INIT_TEXT_SECTION), "ax", @progbits

ASM_GLOBAL_FUNCTION x86_entry_asm
    UNWIND_HINT_UNDEFINED(rip)

    mov ecx, MSR_GS_BASE
    xor eax, eax
    xor edx, edx
    wrmsr

    call x86_entry
ASM_FUNCTION_END x86_entry_asm

.popsection
