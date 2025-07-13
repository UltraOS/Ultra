#pragma once

#include <common/error.h>
#include <arch/registers.h>
#include <private/unwind.h>

/*
 * This is expected to fill the register frame of the state and then jump to
 * a generic unwind_current_begin().
 */
error_t arch_unwind_current_begin(struct unwind_state*, ptr_t starting_pc);

void arch_registers_to_dwarf_registers(struct registers*, ptr_t*);
