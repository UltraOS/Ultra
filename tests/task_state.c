#include <detail/sched/task.h>

static struct task s_test_task;

__thread struct task *g_test_current = &s_test_task;
