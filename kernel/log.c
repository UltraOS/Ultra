#include "common/helpers.h"
#include <common/string.h>
#include <common/format.h>
#include <console.h>
#include <log.h>

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
