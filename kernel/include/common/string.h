#pragma once

#include <common/types.h>
#include <common/attributes.h>

#define memcpy __builtin_memcpy
#define memmove __builtin_memmove
#define memset __builtin_memset
#define memcmp __builtin_memcmp
#define strlen __builtin_strlen
#define strstr __builtin_strstr
#define strcmp __builtin_strcmp

static ALWAYS_INLINE void *memzero(void *dest, size_t count)
{
    return memset(dest, 0, count);
}
