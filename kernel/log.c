#include <common/helpers.h>
#include <common/format.h>
#include <common/error.h>

#include <console.h>
#include <log.h>
#include <symbols.h>
#include <unwind.h>
#include <log_ring.h>

#include <arch/constants.h>

MAKE_LOG_RING(s_log_ring, PAGE_SHIFT + 3, 6);

static char s_hw_id[256] = "Unknown Hardware";

void log_set_hardware_identity_string(const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    vsnprintf(s_hw_id, sizeof(s_hw_id), fmt, va);
    va_end(va);
}

static void print_flush(void)
{
    static char buf[512];
    struct console *con;
    struct log_record rec;
    error_t ret;

    for (con = g_consoles; con; con = con->next) {
        for (;;) {
            ret = log_ring_read(
                &s_log_ring, con->log_seq_num, buf, sizeof(buf), &rec
            );
            if (ret != EOK)
                break;

            con->write(con, buf, rec.length);
            con->log_seq_num = rec.seq_num + 1;
        }
    }
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
    enum log_level level = LOG_LEVEL_DEFAULT;
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
            level = LOG_LEVEL_DEFAULT;
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

static bool do_dump_frame(void *user, ptr_t addr, bool addr_after_call)
{
    struct dump_state *state = user;
    ptr_t lookup_addr;

    lookup_addr = addr_after_call ? addr - 1 : addr;
    print(
        LOG_LEVEL_PREFIX"%c    #%zu in %pSM\n",
        state->level, state->depth++, &lookup_addr
    );

    return true;
}

void dump_stack(enum log_level level, struct registers *regs)
{
    struct dump_state state = {
        .level = level,
        .depth = 0,
    };

    print(LOG_LEVEL_PREFIX"%cHardware: %s\n", level, s_hw_id);
    print(LOG_LEVEL_PREFIX"%cCall trace (most recent call first):\n", level);
    unwind_walk(regs, do_dump_frame, &state);
}
