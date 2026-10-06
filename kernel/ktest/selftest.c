#include <ktest/ktest.h>

#include <sched/kthread.h>
#include <sched/completion.h>

KTEST_CASE(expectations_hold)
{
    struct string name;
    int x = 3;

    name = STR("ktest");

    KTEST_EXPECT(x == 3);
    KTEST_EXPECT_EQ(x + 1, 4);
    KTEST_EXPECT_NE(x, 0);
    KTEST_EXPECT_LT(x, 4);
    KTEST_EXPECT_LE(x, 3);
    KTEST_EXPECT_LE(-x, 0);
    KTEST_EXPECT_GT(x, 2);
    KTEST_EXPECT_GE(x, 3);
    KTEST_EXPECT_LT(-x, 1u);
    KTEST_EXPECT_NE(&x, nullptr);
    KTEST_EXPECT_EQ(name, STR("ktest"));
    KTEST_EXPECT_NE(name, STR("test"));

    KTEST_ASSERT(x > 0);
    KTEST_ASSERT_EQ(x * 2, 6);
    KTEST_ASSERT_EQ(name, STR("ktest"));
}

KTEST_CASE(skipping_is_reported)
{
    ktest_skip("always skipped!");
}

static void expect_from_a_helper(void *user)
{
    struct completion *done = user;

    KTEST_EXPECT(true);
    complete(done);
}

KTEST_CASE(helper_threads_may_expect)
{
    DEFINE_COMPLETION(done);

    KTEST_ASSERT(!is_error(kthread_run(expect_from_a_helper, &done, "helper")));
    wait_for_completion(&done);
}
