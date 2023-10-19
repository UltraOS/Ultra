#include <common/types.h>

#include <module.h>
#include <console.h>
#include <io.h>
#include <arch/private/hypervisor.h>

io_window e9_iow;

static void e9_write(struct console *con, const char *str, size_t count)
{
    UNUSED(con);
    iowrite8_many(e9_iow, 0, (const u8*)str, count);
}

static struct console e9_console = {
    .name = "E9 debugcon",
    .write = e9_write,
};

static error_t e9_console_init(void)
{
    error_t ret = EOK;

    if (!in_hypervisor())
        return ret;

    ret = io_window_map_pio(&e9_iow, 0xE9, 1);
    if (unlikely(ret))
        return ret;

    if (ioread8(e9_iow) != 0xE9)
        goto unmap;

    ret = register_console(&e9_console);
    if (unlikely(ret))
        goto unmap;

    return ret;

unmap:
    io_window_unmap(&e9_iow);
    return ret;
}
INITCALL_EARLYCON(e9_console_init);

static error_t e9_console_fini(void)
{
    unregister_console(&e9_console);
    return EOK;
}
MODULE_FINI(e9_console_fini);
