#pragma once

#include <common/types.h>

/*
 * Call from anything that may sleep, whether or not it ends up doing
 * so, to catch callers that are not allowed to.
 */
void might_sleep(void);

// Sleep until at least 'ns' nanoseconds have passed
void sleep_ns(u64 ns);
