#pragma once

/*
 * This instruction should be handled by arch specific code via
 * arch_handle_abortable_instruction()
 */
#define ABORTABLE_INSTRUCTION_HAS_ARCH_HANDLER (1 << 0)

/*
 * Set the respective error code if an exception occurs while executing this
 * instruction.
 */
#define ABORTABLE_INSTRUCTION_ENOSYS_ON_ERROR (1 << 1)
#define ABORTABLE_INSTRUCTION_EFAULT_ON_ERROR (1 << 2)

#ifndef __ASSEMBLER__
#include <common/types.h>
#include <arch/registers.h>

struct abortable_instruction {
    reg_t try_pc;
    reg_t abort_pc;

    u32 flags;
    u32 arch_flags;
};

// Called by the arch-specific exception handling code
bool handle_abortable_instruction(struct registers*);

#endif
