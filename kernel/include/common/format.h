#pragma once

#include <stdarg.h>

#include <common/attributes.h>
#include <common/types.h>
#include <common/error.h>

MAYBE_NERR(int) vsnprintf(
    char *restrict buffer, size_t capacity, const char *fmt, va_list vlist
);

static inline MAYBE_NERR(int) vscnprintf(
    char *restrict buffer, size_t capacity, const char *fmt, va_list vlist
)
{
    int would_have_been_written;

    would_have_been_written = vsnprintf(buffer, capacity, fmt, vlist);

    if (is_error(would_have_been_written < 0))
        return would_have_been_written;
    if ((size_t)would_have_been_written < capacity)
        return would_have_been_written;

    return capacity ? capacity - 1 : 0;
}

PRINTF_DECL(3, 4)
static inline MAYBE_NERR(int) snprintf(
    char *restrict buffer, size_t capacity, const char *fmt, ...
)
{
    va_list list;
    int written;
    va_start(list, fmt);
    written = vsnprintf(buffer, capacity, fmt, list);
    va_end(list);

    return written;
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
