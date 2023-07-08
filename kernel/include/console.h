#pragma once
#include <common/types.h>
#include <common/error.h>

struct console {
    const char *name;
    void (*write)(struct console *con, const char *str, size_t count);

    struct console *next;
};

error_t register_console(struct console *con);
error_t unregister_console(struct console *con);

void console_write(const char *str, size_t count);
