#include <common/helpers.h>
#include <common/types.h>
#include <common/atomic.h>
#include <common/format.h>

#include <panic.h>
#include <log.h>

static bool g_in_panic;

#undef panic
void panic(const char *fmt, ...)
{
    va_list vlist;
    struct nested_printf npf;

    if (atomic_xchg(&g_in_panic, true, MO_ACQ_REL))
        goto hang;

    va_start(vlist, fmt);
    npf.fmt = fmt;
    npf.vlist = &vlist;

    pr_emerg("Kernel panic: %pV\n", &npf);
    va_end(vlist);

    dump_stack(LOG_LEVEL_EMERG, NULL);

hang:
    for (;;);
}
