#include <console.h>

struct console *g_consoles;

static bool console_registered(struct console *con)
{
    struct console *this_con;

    for (this_con = g_consoles; this_con; this_con = this_con->next) {
        if (this_con == con)
            return true;
    }

    return false;
}

error_t register_console(struct console *con)
{
    if (!g_consoles) {
        g_consoles = con;
        return EOK;
    }

    if (console_registered(con))
        return EBUSY;

    con->next = g_consoles;
    g_consoles = con;
    return EOK;
}

error_t unregister_console(struct console *con)
{
    struct console *cur_con;
    struct console *prev_con = NULL;

    for (cur_con = g_consoles; cur_con; cur_con = cur_con->next) {
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
