#include <common/helpers.h>

#include <panic.h>

void vpanic(const char *reason, va_list vlist)
{
    UNREFERENCED_PARAMETER(reason);
    UNREFERENCED_PARAMETER(vlist);
    for (;;);
}

void panic(const char *reason, ...)
{
    va_list vlist;

    va_start(vlist, reason);
    vpanic(reason, vlist);
    va_end(vlist);
}
