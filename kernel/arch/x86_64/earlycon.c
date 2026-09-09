#define MSG_FMT(msg) "earlycon: " msg

#include <common/error.h>
#include <common/helpers.h>
#include <common/types.h>

#include <module.h>
#include <console.h>
#include <param.h>
#include <arch/private/cpu.h>

#include <memory/io.h>

static io_window s_earlycon_iow;

static void e9_write(struct console *con, const char *str, size_t count)
{
    UNREFERENCED_PARAMETER(con);
    iowrite8_relaxed_many(&s_earlycon_iow, 0, (const u8*)str, count);
}

static struct console e9_console = {
    .name = "E9 debugcon",
    .write = e9_write,
};

// The console currently registered, it owns s_earlycon_iow
static struct console *s_active_console;

static error_t earlycon_activate(struct console *con, bool colored)
{
    error_t ret;

    con->flags = colored ? CONSOLE_FLAG_ANSI_COLOR : CONSOLE_FLAG_NONE;

    ret = register_console(con);
    if (is_error(ret))
        return ret;

    s_active_console = con;
    return EOK;
}

static error_t earlycon_destroy(void)
{
    error_t ret;

    if (s_active_console == nullptr)
        return EOK;

    ret = unregister_console(s_active_console);
    if (is_error(ret))
        return ret;

    io_window_unmap(&s_earlycon_iow);
    s_active_console = nullptr;
    return EOK;
}

static error_t e9_console_init(bool colored)
{
    error_t ret;

    if (!all_cpus_have(X86_FEATURE_HYPERVISOR))
        return ENODEV;

    ret = io_window_map_pio(0xE9, 1, &s_earlycon_iow);
    if (is_error(ret))
        return ret;

    ret = ENODEV;
    if (ioread8(&s_earlycon_iow, 0) != 0xE9)
        goto unmap;

    ret = earlycon_activate(&e9_console, colored);
    if (is_error(ret))
        goto unmap;

    return EOK;

unmap:
    io_window_unmap(&s_earlycon_iow);
    return ret;
}

enum earlycon_mode {
    EARLYCON_MODE_NONE,
    EARLYCON_MODE_E9,
};

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

    UNREFERENCED_PARAMETER(v);

    ret = parse_suboptions_by_head(
        value, modes, ARRAY_SIZE(modes), &mode, is_runtime
    );
    if (is_error(ret))
        return ret;

    ret = earlycon_destroy();
    if (is_error(ret))
        return ret;

    if (mode == EARLYCON_MODE_E9) {
        ret = e9_console_init(colored);
        if (is_error(ret))
            return ret;
    }

    if (mode == EARLYCON_MODE_NONE)
        return EOK;

    pr_info(
        "using%s '%pS' as the early console\n",
        colored ? " (colored)" : "", &modes[mode].head
    );
    return EOK;
}
init_action_parameter(earlycon, earlycon_set);
