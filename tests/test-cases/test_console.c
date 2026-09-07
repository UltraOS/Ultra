#include <kernel-source/console.c>

#include <test_harness.h>

static void dummy_write(struct console *con, const char *str, size_t count)
{
    UNREFERENCED_PARAMETER(con);
    UNREFERENCED_PARAMETER(str);
    UNREFERENCED_PARAMETER(count);
}

static struct console a = { .name = "a", .write = dummy_write };
static struct console b = { .name = "b", .write = dummy_write };
static struct console c = { .name = "c", .write = dummy_write };

static size_t count_registered(void)
{
    struct console *con;
    size_t n = 0;

    for (con = g_consoles; con; con = con->next)
        n++;

    return n;
}

static void reset(void)
{
    g_consoles = nullptr;
    a.next = b.next = c.next = nullptr;
}

TEST_CASE(console_register_unregister_single)
{
    reset();
    ASSERT_EQ(register_console(&a), EOK);
    ASSERT_EQ(g_consoles, &a);
    ASSERT_EQ(unregister_console(&a), EOK);
    ASSERT_NULL(g_consoles);
}

TEST_CASE(console_duplicate_registration_is_rejected)
{
    reset();
    ASSERT_EQ(register_console(&a), EOK);
    ASSERT_EQ(register_console(&a), EBUSY);
    ASSERT_EQ(count_registered(), 1);
}

TEST_CASE(console_unregister_unknown_is_rejected)
{
    reset();
    ASSERT_EQ(register_console(&a), EOK);
    ASSERT_EQ(unregister_console(&b), EINVAL);
    ASSERT_EQ(count_registered(), 1);
}

TEST_CASE(console_unregister_head_middle_tail)
{
    reset();
    ASSERT_EQ(register_console(&a), EOK);
    ASSERT_EQ(register_console(&b), EOK);
    ASSERT_EQ(register_console(&c), EOK);
    ASSERT_EQ(count_registered(), 3);

    // c is the head, b in the middle, a the tail
    ASSERT_EQ(unregister_console(&b), EOK);
    ASSERT_EQ(g_consoles, &c);
    ASSERT_EQ(c.next, &a);

    ASSERT_EQ(unregister_console(&c), EOK);
    ASSERT_EQ(g_consoles, &a);

    ASSERT_EQ(unregister_console(&a), EOK);
    ASSERT_NULL(g_consoles);
}

TEST_CASE(console_reregister_after_unregister)
{
    reset();
    ASSERT_EQ(register_console(&a), EOK);
    ASSERT_EQ(register_console(&b), EOK);

    ASSERT_EQ(unregister_console(&b), EOK);
    ASSERT_EQ(unregister_console(&a), EOK);

    // b still has a stale link to a, registering it must not revive a
    ASSERT_EQ(register_console(&b), EOK);
    ASSERT_EQ(count_registered(), 1);
    ASSERT_NULL(b.next);
}
