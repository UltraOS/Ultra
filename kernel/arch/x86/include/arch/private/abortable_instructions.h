#pragma once

#include <abortable_instructions.h>
#include <linker.h>
#include <common/attributes.h>

// x86_flags
#define ABORTABLE_INSTRUCTION_RDMSR_UNSAFE (1 << 0)
#define ABORTABLE_INSTRUCTION_WRMSR_UNSAFE (1 << 1)

#ifdef __ASSEMBLER__

#define ABORTABLE_INSTRUCTION(x) 1: x

.macro ABORTABLE_INSTRUCTION_RESUME_LABEL insn_label=1b resume_label_name=2 \
                                          flags=0 x86_flags=0
\resume_label_name:

.pushsection SECTION_NAME(ABORTABLE_INSTRUCTIONS_SECTION)

.balign 8
.quad \insn_label
.quad \resume_label_name\()b
.long \flags
.long \x86_flags

.popsection
.endm

#endif
