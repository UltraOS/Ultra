#include <ktest/ktest.h>

#include <common/helpers.h>

#include <sched/kthread.h>
#include <sched/sched.h>
#include <sched/completion.h>
#include <time/time.h>
#include <time/units.h>

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

KTEST_CASE_WITH_TIMEOUT(cases_may_sleep, 1000)
{
    u64 before, after;

    before = ns_since_boot();
    sleep_ns(20 * NS_PER_MS);
    after = ns_since_boot();

    KTEST_EXPECT_GE(after - before, 20 * NS_PER_MS);
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

static void expect_late(void *user)
{
    UNREFERENCED_PARAMETER(user);

    sleep_ns(50 * NS_PER_MS);
    KTEST_EXPECT(false);
}

KTEST_CASE(late_results_are_not_counted)
{
    KTEST_ASSERT(!is_error(kthread_run(expect_late, nullptr, "late")));
}

/*
 * The failure that expect_late() records arrives while this case is running,
 * after late_results_are_not_counted has finished. It must not fail this case
 * because it belongs to the run of the case that started the thread.
 */
KTEST_CASE_WITH_TIMEOUT(late_results_dont_fail_the_running_case, 1000)
{
    sleep_ns(100 * NS_PER_MS);
}

static void mark_index(void *user, u32 index)
{
    bool *ran = user;

    ran[index] = true;
}

KTEST_CASE(threads_have_all_returned_when_the_case_goes_on)
{
    bool ran[4] = { 0 };
    u32 i;

    KTEST_RUN_THREADS(mark_index, ran, ARRAY_SIZE(ran), "marker");

    for (i = 0; i < ARRAY_SIZE(ran); i++)
        KTEST_EXPECT(ran[i]);
}
