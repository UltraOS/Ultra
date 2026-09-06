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

    ret = io_window_map_pio(&s_e9_iow, 0xE9, 1);
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

static const struct string s_earlycon_mode_names[] = {
    [EARLYCON_MODE_NONE] = STR_CONSTEXPR("none"),
    [EARLYCON_MODE_E9] = STR_CONSTEXPR("e9"),
};

static enum earlycon_mode s_earlycon_mode = EARLYCON_MODE_NONE;

static error_t earlycon_destroy(void)
{
    if (s_earlycon_mode == EARLYCON_MODE_E9)
        return unregister_console(&e9_console);

    return EOK;
}

static error_t earlycon_set(
    struct string value, struct param *p, bool is_runtime
)
{
    error_t ret;
    size_t mode;
    enum earlycon_mode *cur = p->value;

    UNREFERENCED_PARAMETER(is_runtime);

    for (mode = 0; mode < ARRAY_SIZE(s_earlycon_mode_names); mode++) {
        if (str_equals_caseless(value, s_earlycon_mode_names[mode]))
            break;
    }
    if (mode == ARRAY_SIZE(s_earlycon_mode_names))
        return EINVAL;

    ret = earlycon_destroy();
    if (is_error(ret))
        return ret;

    if (mode == EARLYCON_MODE_E9) {
        ret = e9_console_init();
        if (is_error(ret))
            return ret;
    }

    *cur = mode;
    if (mode == EARLYCON_MODE_NONE)
        return EOK;

    pr_info(
        "using '%pS' as the early console\n", &s_earlycon_mode_names[mode]
    );
    return EOK;
}

static size_t earlycon_get(struct string *out, struct param *p)
{
    enum earlycon_mode *cur = p->value;

    return param_write_string(out, s_earlycon_mode_names[*cur]);
}

static const struct param_ops s_earlycon_param_ops = {
    .set = earlycon_set,
    .get = earlycon_get,
};
custom_parameter(earlycon, s_earlycon_mode, s_earlycon_param_ops, 0);
