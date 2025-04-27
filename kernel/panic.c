#include <common/helpers.h>

#include <panic.h>

void panic(const char *reason, ...)
{
    UNREFERENCED_PARAMETER(reason);
    for (;;);
}
