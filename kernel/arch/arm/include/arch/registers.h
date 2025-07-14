#pragma once
#include <common/types.h>

struct registers {
    ptr_t x[31];

    ptr_t sp;
    ptr_t pc;
    ptr_t pstate;
};
