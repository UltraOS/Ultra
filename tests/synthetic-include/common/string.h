#pragma once

#include <string.h>

static inline void *memzero(void *dest, size_t count)
{
    return memset(dest, 0, count);
}
