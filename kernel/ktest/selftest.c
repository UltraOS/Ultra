#include <ktest/ktest.h>

#include <sched/kthread.h>
#include <sched/completion.h>

KTEST_CASE(expectations_hold)
{
    const char *name = "ktest";
    int x = 3;

    KTEST_EXPECT(x == 3);
    KTEST_EXPECT_EQ(x + 1, 4);
    KTEST_EXPECT_NE(x, 0);
    KTEST_EXPECT_GE(x, 3);
    KTEST_EXPECT_LE(x, 3);
    KTEST_EXPECT_LE(-x, 0);
    KTEST_EXPECT_STREQ(name, "ktest");

    KTEST_ASSERT(x > 0);
    KTEST_ASSERT_EQ(x * 2, 6);
    KTEST_ASSERT_STREQ(name, "ktest");
}

KTEST_CASE(skipping_is_reported)
{
    ktest_skip("always skipped, exercises the skip path");
}

static void expect_from_a_helper(void *user)
{
    struct completion *done = user;

    KTEST_EXPECT(true);
    complete(done);
}

KTEST_CASE(helper_threads_may_expect)
{
    DEFINE_COMPLETION(done)

    KTEST_ASSERT(!is_error(kthread_run(expect_from_a_helper, &done, "helper")));
    wait_for_completion(&done);
}
