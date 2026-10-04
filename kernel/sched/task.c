#include <private/sched/task.h>

#include <common/format.h>

#include <smp.h>
#include <init_level.h>
#include <free_after_init.h>

DEFINE_PER_CPU(struct task, g_this_cpu_idle_task) = {
    .name = "idle/0",
};

static error_t INIT_CODE idle_tasks_set_names(void)
{
    struct task *task;
    cpu_t cpu;

    for_each_present_cpu(cpu) {
        task = per_cpu_ptr(&g_this_cpu_idle_task, cpu);
        snprintf(task->name, sizeof(task->name), "idle/%u", cpu);
    }

    return EOK;
}
INIT_CALL_POST(PER_CPU_AVAILABLE, idle_tasks_set_names);

const char *task_name(const struct task *task)
{
    return task->name;
}
