#pragma once

#include <stdarg.h>
#include <stdio.h>

#include <common/error.h>
#include <common/attributes.h>

static inline MAYBE_NERR(int) vscnprintf(
    char *restrict buffer, size_t capacity, const char *fmt, va_list vlist
)
{
    int would_have_been_written;

    would_have_been_written = vsnprintf(buffer, capacity, fmt, vlist);

    if (is_nerror(would_have_been_written))
        return would_have_been_written;
    if ((size_t)would_have_been_written < capacity)
        return would_have_been_written;

    return capacity ? capacity - 1 : 0;
}

PRINTF_DECL(3, 4)
static inline MAYBE_NERR(int) scnprintf(
    char *restrict buffer, size_t capacity, const char *fmt, ...
)
{
    va_list list;
    int written;

    va_start(list, fmt);
    written = vscnprintf(buffer, capacity, fmt, list);
    va_end(list);

    return written;
}
