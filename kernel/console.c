#include <console.h>

static struct console *consoles;

static bool console_registered(struct console *con)
{
    struct console *this_con;

    for (this_con = consoles; this_con; this_con = this_con->next) {
        if (this_con == con)
            return true;
    }

    return false;
}

error_t register_console(struct console *con)
{
    if (!consoles) {
        consoles = con;
        return EOK;
    }

    if (console_registered(con))
        return EBUSY;

    con->next = consoles;
    consoles = con;
    return EOK;
}

error_t unregister_console(struct console *con)
{
    struct console *cur_con;
    struct console *prev_con = NULL;

    for (cur_con = consoles; cur_con; cur_con = cur_con->next) {
        if (cur_con != con) {
            prev_con = cur_con;
            continue;
        }

        if (prev_con)
            prev_con->next = cur_con->next;
        return EOK;
    }

    return EINVAL;
}

void console_write(const char *str, size_t count)
{
    struct console *con;

    for (con = consoles; con; con = con->next)
        con->write(con, str, count);
}
