#pragma once
#include <common/types.h>
#include <common/error.h>
#include <common/bit.h>

enum console_flags : u32 {
    CONSOLE_FLAG_NONE = 0,

    /*
     * The sink interprets ECMA-48 select graphic rendition sequences,
     * log output written to it is colored
     */
    CONSOLE_FLAG_ANSI_COLOR = BIT_U32(0),

    /*
     * The sink ignores the faint attribute (SGR 2), dim text is
     * sent to it as dark grey instead
     */
    CONSOLE_FLAG_ANSI_NO_DIM = BIT_U32(1),
};

struct console {
    const char *name;
    void (*write)(struct console *con, const char *str, size_t count);
    void *priv;
    enum console_flags flags;

    u64 log_seq_num;
    struct console *next;
};
extern struct console *g_consoles;

error_t register_console(struct console *con);
error_t unregister_console(struct console *con);
