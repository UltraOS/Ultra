#pragma once

#include <common/types.h>

struct spinlock {
    union {
        u32 control;

        struct {
        #ifdef ULTRA_ARCH_LITTLE_ENDIAN
            u16 last_freed_ticket;
            u16 current_ticket;
        #else
            u16 current_ticket;
            u16 last_freed_ticket;
        #endif
        };
    };
};
