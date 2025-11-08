#pragma once

#include <abortable_instructions.h>
#include <arch/registers.h>

/*
 * Called by generic code if ABORTABLE_INSTRUCTION_HAS_ARCH_HANDLER is set.
 * Returns true if the instruction was handled entirely by arch-specific code
 * and doesn't need further action.
 */
bool arch_handle_abortable_instruction(
    struct registers *regs, const struct abortable_instruction *ai
);
