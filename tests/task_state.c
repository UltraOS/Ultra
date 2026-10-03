#include <detail/sched/task.h>

#include <test_harness.h>

static struct task s_test_task;

__thread struct task *g_test_current = &s_test_task;

void test_task_reset(void)
{
    g_test_current = &s_test_task;
    s_test_task.preempt_count = 0;
}
