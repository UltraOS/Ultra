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

    pr_info("Booting ultra kernel v0.1-%s for %s...\n",
            ULTRA_GIT_SHA, ULTRA_ARCH_EXECUTION_MODE_STRING);
    for (;;);
}
