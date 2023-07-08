#include <boot/entry.h>
#include <initcall.h>
#include <log.h>

static void do_initcalls(initcall_t *begin, initcall_t *end)
{
    initcall_t *fn;

    for (fn = begin; fn < end; fn++) {
        (*fn)();
    }
}

void entry(void)
{
    do_initcalls(initcalls_earlycon_begin, initcalls_earlycon_end);

    infoln("Booting ultra kernel v0.1-%s for %s...",
           ULTRA_GIT_SHA, ULTRA_ARCH_STRING);
    for (;;);
}
