#pragma once

#include <common/error.h>
#include <arch/registers.h>
#include <private/unwind.h>

/*
 * This is expected to fill the register frame of the state and then jump to
 * a generic unwind_current_begin().
 */
error_t arch_unwind_current_begin(struct unwind_state*, ptr_t starting_pc);

/*
 * Best effort attempt to recover the previous call frame register
 * state to proceed unwinding through e.g. a NULL/corrupted function
 * pointer that standard dwarf can't handle.
 */
error_t arch_try_recover_previous_frame(struct unwind_state *state);

void arch_registers_to_dwarf_registers(struct registers*, ptr_t*);
