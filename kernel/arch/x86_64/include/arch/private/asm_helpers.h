#pragma once

#include <asm_helpers.h>

.macro ASM_PRELUDE
    .intel_syntax noprefix
    .section ".text", "ax", @progbits
.endm
