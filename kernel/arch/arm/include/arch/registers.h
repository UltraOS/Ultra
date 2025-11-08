#pragma once
#include <common/types.h>

struct registers {
    ptr_t x[31];

    ptr_t sp;
    ptr_t pc;
    ptr_t pstate;
};

static inline reg_t registers_get_pc(struct registers *regs)
{
    return regs->pc;
}

static inline void registers_set_pc(struct registers *regs, u64 value)
{
    regs->pc = value;
}

static inline void registers_set_return_value(struct registers *regs, u64 value)
{
    regs->x[0] = value;
}
