#define MSG_FMT(msg) "init-level: " msg

#include <free_after_init.h>
#include <init_level.h>
#include <bug.h>
#include <config.h>
#include <log.h>

#include <common/atomic.h>

#define INIT_LEVEL_CB_ARRAY_MARKER(lvl, type, kind) \
    LINKER_SYMBOL(CONCAT(INIT_LEVEL_CB_SECTION(lvl, type), _##kind))

#define INIT_LEVEL(x)                                                      \
    extern const init_call_t INIT_LEVEL_CB_ARRAY_MARKER(x, pre, begin)[];  \
    extern const init_call_t INIT_LEVEL_CB_ARRAY_MARKER(x, pre, end)[];    \
    extern const init_call_t INIT_LEVEL_CB_ARRAY_MARKER(x, post, begin)[]; \
    extern const init_call_t INIT_LEVEL_CB_ARRAY_MARKER(x, post, end)[];

INIT_LEVELS
#undef INIT_LEVEL

struct init_calls {
    const init_call_t *pre_begin, *pre_end;
    const init_call_t *post_begin, *post_end;
};

#define INIT_LEVEL(x)                             \
    [INIT_LEVEL_##x] = {                          \
      INIT_LEVEL_CB_ARRAY_MARKER(x, pre, begin),  \
      INIT_LEVEL_CB_ARRAY_MARKER(x, pre, end),    \
      INIT_LEVEL_CB_ARRAY_MARKER(x, post, begin), \
      INIT_LEVEL_CB_ARRAY_MARKER(x, post, end),   \
    },

static const struct init_calls INIT_RODATA s_init_calls[] = {
    INIT_LEVELS
    #undef INIT_LEVEL
};

static enum init_level s_init_level = INIT_LEVEL_NONE;
static bool s_deferred_init_level_raise_pending = false;
static bool s_in_progress = false;

#if IS_ENABLED(VERBOSE_INIT_LEVELS)

static const char *const INIT_RODATA s_init_level_to_string[NUM_INIT_LEVELS] = {
    #define INIT_LEVEL(x) [INIT_LEVEL_##x] = #x,
    INIT_LEVELS
    #undef INIT_LEVEL
};

static void INIT_CODE trace_init_level_raise_begin(
    enum init_level level, size_t num_callbacks
)
{
    pr_debug(
        "raising %s => %s (%zu callback%s to reach)\n",
        s_init_level_to_string[level - 1],
        s_init_level_to_string[level], num_callbacks,
        num_callbacks == 1 ? "" : "s"
    );
}

static void INIT_CODE trace_init_level_raise_end(
    enum init_level level, size_t num_callbacks
)
{
    pr_debug(
        "%s reached (%zu post callback%s to run)\n",
        s_init_level_to_string[level], num_callbacks,
        num_callbacks == 1 ? "" : "s"
    );
}

static void INIT_CODE trace_callback_start(const init_call_t *cb)
{
    pr_debug("    > entering init call %pSM\n", cb);
}

static void INIT_CODE trace_callback_finish(
    const init_call_t *cb, error_t ret, bool was_pending
)
{
    const char *raise_msg = "";
    enum log_level lvl = LOG_LEVEL_DEBUG;

    if (!was_pending && s_deferred_init_level_raise_pending)
        raise_msg = ", queued init level raise";
    if (is_error(ret))
        lvl = LOG_LEVEL_WARN;

    pr_lvl(
        lvl, "    < leaving init call %pSM (ret=%d%s)\n",
        cb, ret, raise_msg
    );
}

#else

static void trace_init_level_raise_begin(
    enum init_level level, size_t num_callbacks
)
{
    UNREFERENCED_PARAMETER(level);
    UNREFERENCED_PARAMETER(num_callbacks);
}

static void trace_init_level_raise_end(
    enum init_level level, size_t num_callbacks
)
{
    UNREFERENCED_PARAMETER(level);
    UNREFERENCED_PARAMETER(num_callbacks);
}

static void INIT_CODE trace_callback_start(const init_call_t *cb)
{
    UNREFERENCED_PARAMETER(cb);
}

static void INIT_CODE trace_callback_finish(
    const init_call_t *cb, error_t ret, bool was_pending
)
{
    UNREFERENCED_PARAMETER(cb);
    UNREFERENCED_PARAMETER(ret);
    UNREFERENCED_PARAMETER(was_pending);
}

#endif

enum init_level init_level(void)
{
    return atomic_load_relaxed(&s_init_level);
}

bool init_level_at_least(enum init_level level)
{
    return init_level() >= level;
}

bool init_level_below(enum init_level level)
{
    return init_level() < level;
}

static void INIT_CODE run_one_callback(const init_call_t *cb)
{
    error_t ret;
    bool was_pending = s_deferred_init_level_raise_pending;

    trace_callback_start(cb);
    ret = (*cb)();
    trace_callback_finish(cb, ret, was_pending);

    /*
     * This information is already logged in trace_callback_finish if it's
     * enabled, don't duplicate for no reason
     */
#if !IS_ENABLED(VERBOSE_INIT_LEVELS)
    if (is_error(ret))
        pr_warn("callback %pSM failed: %d\n", cb, ret);
#endif
}

void INIT_CODE init_level_raise(enum init_level lvl)
{
    const struct init_calls *cbs;
    const init_call_t *cb;
    enum init_level cur;

    BUG_ON(lvl <= init_level());
    BUG_ON(lvl >= NUM_INIT_LEVELS);
    BUG_ON(s_in_progress || s_deferred_init_level_raise_pending);

    s_in_progress = true;

    for (cur = init_level() + 1; cur <= lvl; cur++) {
        cbs = &s_init_calls[cur];

        trace_init_level_raise_begin(cur, cbs->pre_end - cbs->pre_begin);

        for (cb = cbs->pre_begin; cb < cbs->pre_end; cb++)
            run_one_callback(cb);

        atomic_store_relaxed(&s_init_level, cur);

        trace_init_level_raise_end(cur, cbs->post_end - cbs->post_begin);

        for (cb = cbs->post_begin; cb < cbs->post_end; cb++)
            run_one_callback(cb);

        if (s_deferred_init_level_raise_pending) {
            // The callback we have just invoked queued an init level raise
            s_deferred_init_level_raise_pending = false;
            if (lvl <= cur)
                lvl = cur + 1;
        }
    }

    s_in_progress = false;
}

void INIT_CODE init_level_raise_deferred(enum init_level next_level)
{
    BUG_ON(next_level != init_level() + 1);
    s_deferred_init_level_raise_pending = true;
}
