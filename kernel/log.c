#include <common/helpers.h>
#include <common/format.h>
#include <common/error.h>

#include <console.h>
#include <log.h>
#include <symbols.h>
#include <unwind.h>

static size_t extract_msg_level(const char *msg, enum log_level *out_level)
{
    u8 level;

    if (msg[0] != LOG_LEVEL_PREFIX_CHAR)
        return 0;
    if (unlikely(msg[1] == '\0'))
        return 1;

    level = msg[1] - '0';
    if (likely(level < LOG_LEVEL_COUNT))
        *out_level = level;

    return 2;
}

void vprint(const char *msg, va_list vlist)
{
    static char log_buf[256];
    enum log_level level = LOG_LEVEL_DEFAULT;
    int chars;

    if (unlikely(!msg))
        return;

    msg += extract_msg_level(msg, &level);

    chars = vscnprintf(log_buf, sizeof(log_buf), msg, vlist);
    if (unlikely(chars < 0))
        return;

    console_write(log_buf, chars);
}

void print(const char *msg, ...)
{
    va_list vlist;
    va_start(vlist, msg);
    vprint(msg, vlist);
    va_end(vlist);
}

struct dump_state {
    enum log_level level;
    size_t depth;
};

static bool do_dump_frame(void *user, ptr_t addr, bool addr_after_call)
{
    struct dump_state *state = user;
    ptr_t lookup_addr;

    lookup_addr = addr_after_call ? addr - 1 : addr;
    print("    #%zu in %pSM\n", state->depth++, &lookup_addr);

    return true;
}

void dump_stack(enum log_level level, struct registers *regs)
{
    struct dump_state state = {
        // FIXME: support extracting the log level via %s
        .level = level,
        .depth = 0,
    };

    print("Call trace (most recent call first):\n");
    unwind_walk(regs, do_dump_frame, &state);
}
