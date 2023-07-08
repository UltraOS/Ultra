#include <common/string.h>
#include <common/format.h>
#include <console.h>
#include <log.h>

#define PREFIX_SIZE 7
#define INFO_PREFIX "[INFO] "
#define WARN_PREFIX "[WARN] "
#define ERR_PREFIX "[ERR ] "

static char (level_to_prefix[LOG_LEVEL_MAX + 1])[PREFIX_SIZE] = {
    [LOG_LEVEL_INFO] = INFO_PREFIX,
    [LOG_LEVEL_WARN] = WARN_PREFIX,
    [LOG_LEVEL_ERR] = ERR_PREFIX,
};

void vprint(enum log_level lvl, const char *msg, va_list vlist)
{
    static char log_buf[256];
    char *buf = log_buf;
    int chars = sizeof(log_buf);

    if (unlikely(!msg))
        return;
    if (unlikely((u32)lvl > LOG_LEVEL_MAX))
        return;

    memcpy(buf, level_to_prefix[lvl], PREFIX_SIZE);
    buf += PREFIX_SIZE;
    chars -= PREFIX_SIZE;

    chars = vscnprintf(buf, chars, msg, vlist);
    if (unlikely(chars < 0))
        return;
    chars += PREFIX_SIZE;

    console_write(log_buf, chars);
}

void print(enum log_level lvl, const char *msg, ...)
{
    va_list vlist;
    va_start(vlist, msg);
    vprint(lvl, msg, vlist);
    va_end(vlist);
}
