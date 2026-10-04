#pragma once

#include <common/types.h>

struct registers {
    irq_state_t irq_state;
};

static inline irq_state_t registers_get_irq_state(struct registers *regs)
{
    return regs->irq_state;
}
