#include <boot/entry.h>
#include <initcall.h>

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
    for (;;);
}
