#include <common/helpers.h>

#include <panic.h>

void panic(const char *reason, ...)
{
    UNUSED(reason);
    for (;;);
}
