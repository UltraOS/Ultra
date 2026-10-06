#include <kernel-source/ktest/helpers.c>

#include <test_harness.h>

static bool selected(const char *suite, const char *name, const char *selection)
{
    return ktest_case_is_selected(
        STR_RUNTIME(suite), STR_RUNTIME(name), STR_RUNTIME(selection)
    );
}

TEST_CASE(ktest_select_all)
{
    ASSERT(selected("sched", "slice", "all"));
    ASSERT(selected("sched", "slice", "mutex,all"));
    ASSERT(!selected("sched", "slice", ""));
}

TEST_CASE(ktest_select_by_suite)
{
    ASSERT(selected("sched", "slice", "sched"));
    ASSERT(selected("sched", "slice", "mutex,sched"));
    ASSERT(!selected("sched", "slice", "mutex"));
    ASSERT(!selected("sched", "slice", "sche"));
    ASSERT(!selected("sched", "slice", "scheduler"));
}

TEST_CASE(ktest_select_by_case)
{
    ASSERT(selected("sched", "slice", "sched.slice"));
    ASSERT(selected("sched", "slice", "mutex.handoff,sched.slice"));
    ASSERT(!selected("sched", "slice", "sched.sleep"));
    ASSERT(!selected("sched", "slice", "mutex.slice"));
    ASSERT(!selected("sched", "slice", "sched."));
}

TEST_CASE(ktest_select_ignores_empty_tokens)
{
    ASSERT(selected("sched", "slice", ",sched,"));
    ASSERT(!selected("sched", "slice", ","));
}
