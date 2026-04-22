#pragma once

#include <arch/private/unwind_hints.h>

.macro ASM_PRELUDE
    .intel_syntax noprefix
    .section ".text", "ax", @progbits
.endm

.macro ASM_FUNCTION name, is_global=0, align=0, unwind_hint=0
    .type \name, @function

    .if \is_global
        .globl \name
    .endif

    .if \align
        .balign \align
    .endif

    \name:

    .if \unwind_hint
        UNWIND_HINT_START_OF_FUNCTION
    .endif
.endm

.macro ASM_LOCAL_FUNCTION name, align=0, unwind_hint=1
    ASM_FUNCTION \name, 0, \align, \unwind_hint
.endm

.macro ASM_GLOBAL_FUNCTION name, align=0, unwind_hint=1
    ASM_FUNCTION \name, 1, \align, \unwind_hint
.endm

.macro ASM_FUNCTION_END name, unwind_hint=1
    .if \unwind_hint
        UNWIND_HINT_END_OF_FUNCTION
    .endif
    .size \name, . - \name
.endm
