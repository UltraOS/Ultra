#define MSG_FMT(msg) "earlycon: " msg

#include <common/error.h>
#include <common/helpers.h>
#include <common/types.h>

#include <module.h>
#include <console.h>
#include <param.h>
#include <arch/private/cpu.h>

#include <memory/io.h>

static io_window s_e9_iow;

static void e9_write(struct console *con, const char *str, size_t count)
{
    UNREFERENCED_PARAMETER(con);
    iowrite8_many(s_e9_iow, 0, (const u8*)str, count);
}

static struct console e9_console = {
    .name = "E9 debugcon",
    .write = e9_write,
};

static error_t e9_console_init(void)
{
    error_t ret = EOK;

    if (!all_cpus_have(X86_FEATURE_HYPERVISOR))
        return ENODEV;

    ret = io_window_map_pio(&s_e9_iow, 0xE9, 1);
    if (is_error(ret))
        return ret;

    if (ioread8(s_e9_iow) != 0xE9)
        goto unmap;

    ret = register_console(&e9_console);
    if (unlikely(ret))
        goto unmap;

    return ret;

unmap:
    io_window_unmap(&s_e9_iow);
    return ret;
}

#define EARLYCON_MODE_NONE STR_CONSTEXPR("none")
#define EARLYCON_MODE_E9 STR_CONSTEXPR("e9")

struct string g_earlycon = EARLYCON_MODE_NONE;

static error_t earlycon_destroy(void)
{
    if (str_equals_caseless(g_earlycon, EARLYCON_MODE_E9))
        return unregister_console(&e9_console);

    return EOK;
}

static error_t earlycon_set(struct string mode, struct param *p)
{
    error_t ret;
    struct string *cur = p->value;

    ret = earlycon_destroy();
    if (is_error(ret))
        return ret;

    if (str_equals_caseless(mode, EARLYCON_MODE_NONE)) {
        *cur = EARLYCON_MODE_NONE;
        return EOK;
    }

    if (str_equals_caseless(mode, EARLYCON_MODE_E9)) {
        ret = e9_console_init();
        if (is_error(ret))
            return ret;

        *cur = EARLYCON_MODE_E9;
        goto out_ok;
    }

    return EINVAL;

out_ok:
    pr_info("using '%pS' as the early console\n", cur);
    return EOK;
}

static const struct param_ops g_earlycon_ops = {
    .set = earlycon_set,
    .get = param_get_string,
};
early_parameter_with_ops(g_earlycon, g_earlycon_ops);
