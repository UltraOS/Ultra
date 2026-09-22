#include <common/helpers.h>
#include <common/format.h>
#include <common/error.h>
#include <common/minmax.h>
#include <common/string.h>
#include <common/ctype.h>

#include <console.h>
#include <log.h>
#include <symbols.h>
#include <unwind.h>
#include <log_ring.h>
#include <param.h>

#include <time/units.h>

#include <arch/constants.h>

MAKE_LOG_RING(s_log_ring, PAGE_SHIFT + 3, 6);

static const struct suboption_variant s_log_level_variants[] = {
    [LOG_LEVEL_EMERG]  = SUBOPTION_VARIANT("emergency"),
    [LOG_LEVEL_ALERT]  = SUBOPTION_VARIANT("alert"),
    [LOG_LEVEL_CRIT]   = SUBOPTION_VARIANT("critical"),
    [LOG_LEVEL_ERR]    = SUBOPTION_VARIANT("error"),
    [LOG_LEVEL_WARN]   = SUBOPTION_VARIANT("warning"),
    [LOG_LEVEL_NOTICE] = SUBOPTION_VARIANT("notice"),
    [LOG_LEVEL_INFO]   = SUBOPTION_VARIANT("info"),
    [LOG_LEVEL_DEBUG]  = SUBOPTION_VARIANT("debug"),
};
static enum log_level s_log_level = LOG_LEVEL_CONSOLE_DEFAULT;

static error_t log_level_set(
    struct string value, struct param_value *v, bool is_runtime
)
{
    error_t ret;
    enum log_level *cur = v->ptr;
    size_t log_level_idx;

    ret = parse_suboptions_by_head(
        value, s_log_level_variants, ARRAY_SIZE(s_log_level_variants),
        &log_level_idx, is_runtime
    );
    if (is_error(ret))
        return ret;

    *cur = log_level_idx;
    return EOK;
}

static size_t log_level_get(struct string *out, const struct param_value *val)
{
    enum log_level *cur = val->ptr;

    return param_write_string(out, s_log_level_variants[*cur].head);
}

static struct param_ops s_log_level_ops = {
    .set = log_level_set,
    .get = log_level_get,
};
parameter_with_ops_and_flags(
    s_log_level, s_log_level_ops, PARAM_RUNTIME_WRITABLE
);

static char s_hw_id[256] = "Unknown Hardware";

void log_set_hardware_identity_string(const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    vsnprintf(s_hw_id, sizeof(s_hw_id), fmt, va);
    va_end(va);
}

// Select graphic rendition parameters, combined with ";"
#define ATTR_RESET "0"
#define ATTR_BOLD "1"
#define ATTR_DIM "2"
#define FG_RED "31"
#define FG_YELLOW "33"
#define FG_BLUE "34"
#define FG_BRIGHT_BLACK "90"
#define FG_BRIGHT_WHITE "97"
#define BG_RED "41"

#define SGR(params) "\x1b[" params "m"
#define SGR_RESET SGR(ATTR_RESET)
#define SGR_BOLD_RED SGR(ATTR_BOLD ";" FG_RED)
#define SGR_PREFIX SGR(ATTR_BOLD ";" FG_BLUE)

static const char *const s_level_sgr[LOG_LEVEL_COUNT] = {
    [LOG_LEVEL_EMERG] = SGR(FG_BRIGHT_WHITE ";" BG_RED),
    [LOG_LEVEL_ALERT] = SGR_BOLD_RED,
    [LOG_LEVEL_CRIT] = SGR_BOLD_RED,
    [LOG_LEVEL_ERR] = SGR(FG_RED),
    [LOG_LEVEL_WARN] = SGR(ATTR_BOLD ";" FG_YELLOW),
    [LOG_LEVEL_NOTICE] = SGR(ATTR_BOLD),
    [LOG_LEVEL_DEBUG] = SGR(ATTR_DIM),
};

static const char *level_sgr(u8 level, enum console_flags flags)
{
    if (level >= LOG_LEVEL_COUNT)
        return nullptr;

    if (level == LOG_LEVEL_DEBUG && (flags & CONSOLE_FLAG_ANSI_NO_DIM))
        return SGR(FG_BRIGHT_BLACK);

    return s_level_sgr[level];
}

#define MAX_LOG_PREFIX_LENGTH 16

/*
 * Length of the "subsys: " prefix at the start of a message including the
 * colon, zero if the message doesn't start with one
 */
static size_t log_prefix_length(const char *msg, size_t len)
{
    size_t i;

    if (len == 0 || !islower(msg[0]))
        return 0;

    for (i = 1; i < len && i < MAX_LOG_PREFIX_LENGTH; i++) {
        char c = msg[i];

        if (islower(c) || isdigit(c) || c == '-')
            continue;

        if (c == ':' && (i + 1) < len && msg[i + 1] == ' ')
            return i + 1;

        break;
    }

    return 0;
}

struct out_buf {
    char *data;
    size_t size, capacity;
};

static void out_buf_append(struct out_buf *buf, const char *str, size_t len)
{
    len = MIN(len, buf->capacity - buf->size);
    memcpy(buf->data + buf->size, str, len);
    buf->size += len;
}

static void out_buf_append_cstr(struct out_buf *buf, const char *str)
{
    out_buf_append(buf, str, strlen(str));
}

static void format_record(
    struct out_buf *out, const char *stamp, size_t stamp_len,
    const char *msg, size_t len, u8 level, enum console_flags flags
)
{
    size_t prefix_len;
    bool has_newline;
    const char *sgr;

    out_buf_append(out, stamp, stamp_len);

    if (!(flags & CONSOLE_FLAG_ANSI_COLOR)) {
        out_buf_append(out, msg, len);
        return;
    }

    // The reset and the newline that end the line must always fit
    out->capacity -= sizeof(SGR_RESET);

    prefix_len = log_prefix_length(msg, len);
    if (prefix_len) {
        out_buf_append_cstr(out, SGR_PREFIX);
        out_buf_append(out, msg, prefix_len);
        out_buf_append_cstr(out, SGR_RESET);

        msg += prefix_len;
        len -= prefix_len;
    }

    has_newline = len && msg[len - 1] == '\n';
    len -= has_newline;

    sgr = level_sgr(level, flags);
    if (sgr)
        out_buf_append_cstr(out, sgr);
    out_buf_append(out, msg, len);

    out->capacity += sizeof(SGR_RESET);
    out_buf_append_cstr(out, SGR_RESET);
    if (has_newline)
        out_buf_append(out, "\n", 1);
}

void log_flush_console(struct console *con)
{
    static char s_msg_buf[512], s_out_data[512 + 128];

    struct log_record rec;
    struct out_buf out;
    error_t ret;
    u64 sec, usec;
    int stamp_len;
    char stamp[32];

    for (;;) {
        ret = log_ring_read(
            &s_log_ring, con->log_seq_num, s_msg_buf, sizeof(s_msg_buf), &rec
        );
        if (ret != EOK)
            break;

        con->log_seq_num = rec.seq_num + 1;

        if (rec.level > s_log_level)
            continue;

        sec = rec.timestamp_ns / NS_PER_SEC;
        usec = (rec.timestamp_ns % NS_PER_SEC) / 1000;
        stamp_len = snprintf(
            stamp, sizeof(stamp), "[%5llu.%06llu] ", sec, usec
        );

        out = (struct out_buf) {
            .data = s_out_data,
            .capacity = sizeof(s_out_data),
        };
        format_record(
            &out, stamp, stamp_len, s_msg_buf, rec.length, rec.level,
            con->flags
        );

        con->write(con, out.data, out.size);
    }
}

static void print_flush(void)
{
    struct console *con;

    for (con = g_consoles; con; con = con->next)
        log_flush_console(con);
}

static size_t extract_msg_level(const char *msg, enum log_level *out_level)
{
    u8 level;

    if (msg[0] != LOG_LEVEL_PREFIX_CHAR)
        return 0;
    if (unlikely(msg[1] == '\0'))
        return 1;

    level = msg[1] - '0';
    if (likely(level <= LOG_LEVEL_COUNT))
        *out_level = level;

    return 2;
}

void vprint(const char *msg, va_list vlist)
{
    struct log_ring_reservation res;
    int chars;
    char prefix_buf[4];
    va_list vlist_copy;
    error_t ret;
    enum log_level level = LOG_LEVEL_MSG_DEFAULT;
    size_t write_offset = 0, prefix_len = 0;
    bool had_newline = false, is_extended = false;

    if (unlikely(!msg))
        return;

    /*
     * Format the entire log string, but only capture the prefix in order to
     * figure out the requested log level (it may be specified as a format
     * string itself). This is also how we find out the number of bytes to
     * allocate from the log ring.
     */
    va_copy(vlist_copy, vlist);
    chars = vsnprintf(prefix_buf, sizeof(prefix_buf), msg, vlist_copy) + 1;
    va_end(vlist_copy);

    if (unlikely(chars <= 0))
        return;

    prefix_len = extract_msg_level(prefix_buf, &level);
    chars -= prefix_len;

    if (level == LOG_LEVEL_CONTINUED) {
        size_t prev_length = 0;

        ret = log_ring_reserve_extend(&s_log_ring, chars, &res, &prev_length);
        if (ret == EOK) {
            is_extended = true;
            if (prev_length > 0)
                write_offset = prev_length - 1;
        } else {
            /*
             * Something raced against the initial print, so we can no longer
             * extend it. Do a new one from scratch so it's not lost
             * completely.
             */
            level = LOG_LEVEL_MSG_DEFAULT;
        }
    }

    if (!is_extended) {
        ret = log_ring_reserve(&s_log_ring, chars, level, &res);
        if (is_error(ret))
            return;
    }

    res.resident_length = write_offset + vsnprintf_skip_n(
        res.reserved_data + write_offset, chars, msg, vlist, prefix_len
    ) + 1;

    if (res.resident_length >= 2 &&
        res.reserved_data[res.resident_length - 2] == '\n') {
        // Strip the vsnprintf null terminator
        res.resident_length--;
        had_newline = true;
    } else if (res.resident_length >= 1) {
        // Replace the null terminator with a newline if one was missing
        res.reserved_data[res.resident_length - 1] = '\n';
    }

    if (!had_newline) {
        log_ring_commit(&res);
        return;
    }

    log_ring_publish(&res);
    print_flush();
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

#define FRAME_FMT(symbol) "    #%zu in <0x%016zX> at " symbol "\n"

static bool do_dump_frame(void *user, ptr_t addr, bool addr_after_call)
{
    struct dump_state *state = user;

    if (addr_after_call)
        pr_lvl(state->level, FRAME_FMT("%pRSM"), state->depth++, addr, &addr);
    else
        pr_lvl(state->level, FRAME_FMT("%pSM"), state->depth++, addr, &addr);

    return true;
}

void dump_stack(enum log_level level, struct registers *regs)
{
    struct dump_state state = {
        .level = level,
        .depth = 0,
    };

    pr_lvl(level, "Hardware: %s\n", s_hw_id);
    pr_lvl(level, "Call trace (most recent call first):\n");
    unwind_walk(regs, do_dump_frame, &state);
}
