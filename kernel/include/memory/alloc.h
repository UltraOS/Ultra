#pragma once

#include <common/types.h>

enum alloc_behavior {
    /*
     * Generic kernel allocation, may sleep, use IO, reclaim,
     * retry, or otherwise do things that may cause unpredictable
     * delays.
     */
    ALLOC_GENERIC = 0 << 0,

    /*
     * Zero the allocated memory.
     */
    ALLOC_ZEROED = 1 << 0,
};

void *alloc(size_t size, enum alloc_behavior);
void free(void*);
