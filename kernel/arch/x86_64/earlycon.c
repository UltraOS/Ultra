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
    iowrite8_relaxed_many(&s_e9_iow, 0, (const u8*)str, count);
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

    ret = io_window_map_pio(0xE9, 1, &s_e9_iow);
    if (is_error(ret))
        return ret;

    if (ioread8(&s_e9_iow, 0) != 0xE9)
        goto unmap;

    ret = register_console(&e9_console);
    if (unlikely(ret))
        goto unmap;

    return ret;

unmap:
    io_window_unmap(&s_e9_iow);
    return ret;
}

enum earlycon_mode {
    EARLYCON_MODE_NONE,
    EARLYCON_MODE_E9,
};

static enum earlycon_mode s_earlycon = EARLYCON_MODE_NONE;

static error_t earlycon_destroy(void)
{
    if (s_earlycon == EARLYCON_MODE_E9)
        return unregister_console(&e9_console);

    return EOK;
}

static error_t earlycon_set(
    struct string value, struct param_value *v, bool is_runtime
)
{
    error_t ret;
    size_t mode;
    bool colored = false;
    struct suboption options[] = { suboption(colored) };
    struct suboption_variant modes[] = {
        [EARLYCON_MODE_NONE] = SUBOPTION_VARIANT("none"),
        [EARLYCON_MODE_E9] = SUBOPTION_VARIANT("e9", options),
    };
    enum earlycon_mode *cur = v->ptr;

    ret = parse_suboptions_by_head(
        value, modes, ARRAY_SIZE(modes), &mode, is_runtime
    );
    if (is_error(ret))
        return ret;

    ret = earlycon_destroy();
    if (is_error(ret))
        return ret;

    if (mode == EARLYCON_MODE_E9) {
        e9_console.flags =
            colored ? CONSOLE_FLAG_ANSI_COLOR : CONSOLE_FLAG_NONE;

        ret = e9_console_init();
        if (is_error(ret))
            return ret;
    }

    *cur = mode;
    if (mode == EARLYCON_MODE_NONE)
        return EOK;

    pr_info(
        "using%s '%pS' as the early console\n",
        colored ? " (colored)" : "", &modes[mode].head
    );
    return EOK;
}

static const struct param_ops s_earlycon_param_ops = {
    .set = earlycon_set,
};
parameter_with_ops(s_earlycon, s_earlycon_param_ops);
