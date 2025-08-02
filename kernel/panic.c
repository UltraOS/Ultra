#include <common/helpers.h>
#include <common/types.h>
#include <common/atomic.h>
#include <common/format.h>

#include <panic.h>
#include <log.h>

static bool g_in_panic;

void panic(const char *reason, ...)
{
    static char panic_buf[256];
    va_list vlist;

    if (atomic_xchg(&g_in_panic, true, MO_ACQ_REL))
        goto hang;

    va_start(vlist, reason);
    vsnprintf(panic_buf, sizeof(panic_buf), reason, vlist);
    va_end(vlist);

    pr_emerg("Kernel panic: %s", panic_buf);
    dump_stack(LOG_LEVEL_EMERG, NULL);

hang:
    for (;;);
}
