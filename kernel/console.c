#include <console.h>
#include <log.h>

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
    if (console_registered(con))
        return EBUSY;

    con->next = g_consoles;
    g_consoles = con;

    log_flush_console(con);
    return EOK;
}

error_t unregister_console(struct console *con)
{
    struct console *cur_con, *prev_con = nullptr;

    for (cur_con = g_consoles; cur_con; cur_con = cur_con->next) {
        if (cur_con != con) {
            prev_con = cur_con;
            continue;
        }

        if (prev_con)
            prev_con->next = cur_con->next;
        else
            g_consoles = cur_con->next;

        return EOK;
    }

    return EINVAL;
}
