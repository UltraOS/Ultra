#pragma once

#include <common/types.h>
#include <common/overflow.h>

#define NO_DEADLINE UINT64_MAX

u64 ns_since_boot(void);

// A time point 'ns' nanoseconds in the future
static inline u64 ns_from_now(u64 ns)
{
    u64 deadline_ns;

    if (check_add_overflow(ns_since_boot(), ns, &deadline_ns))
        return NO_DEADLINE;

    return deadline_ns;
}

void delay_ns(u64);
