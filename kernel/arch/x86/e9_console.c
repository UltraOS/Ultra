#include <common/error.h>
#include <common/helpers.h>
#include <common/types.h>

#include <module.h>
#include <console.h>
#include <io.h>
#include <arch/private/hypervisor.h>

static void e9_write(struct console *con, const char *str, size_t count)
{
    iowrite8_many(con->priv, 0, (const u8*)str, count);
}

static struct console e9_console = {
    .name = "E9 debugcon",
    .write = e9_write,
};

static error_t e9_console_init(void)
{
    error_t ret = EOK;
    io_window *iow;

    if (!in_hypervisor())
        return ret;

    iow = io_window_map_pio(0xE9, 1);
    if (error_ptr(iow))
        return decode_error_ptr(iow);

    if (ioread8(iow) != 0xE9)
        goto unmap;

    e9_console.priv = iow;

    ret = register_console(&e9_console);
    if (unlikely(ret))
        goto unmap;

    return ret;

unmap:
    e9_console.priv = NULL;
    io_window_unmap(iow);
    return ret;
}
INITCALL_EARLYCON(e9_console_init);

static error_t e9_console_fini(void)
{
    unregister_console(&e9_console);
    return EOK;
}
MODULE_FINI(e9_console_fini);
