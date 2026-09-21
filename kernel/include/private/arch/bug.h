#pragma once

#include <arch/registers.h>

/*
 * Called by arch-specific code that catches the trap produced via
 * ARCH_BUG_TRAP_INSTRUCTION to dispatch into generic bug-handling code.
 * This function returns true if the trap was caused by a WARN(), false
 * otherwise (or doesn't return at all for BUG() callers).
 */
bool bug_handle_trap(struct registers *regs);
