#include <sched/kthread.h>

#include <common/format.h>

#include <private/sched/task.h>
#include <private/sched/core.h>
#include <private/sched/rr.h>
#include <private/arch/task.h>

struct task *kthread_vcreate(
    task_fn_t fn, void *user, const char *name_fmt, va_list vlist
)
{
    struct task *task;

    task = task_alloc();
    if (unlikely(task == nullptr))
        return nullptr;

    vsnprintf(task->name, sizeof(task->name), name_fmt, vlist);

    task->cpu = current->cpu;
    task->prio = TASK_PRIO_DEFAULT;
    task->sched_class = &SCHED_CLASS_INSTANCE(rr);
    arch_task_frame_init(task, fn, user);

    return task;
}

struct task *kthread_create(
    task_fn_t fn, void *user, const char *name_fmt, ...
)
{
    struct task *task;
    va_list vlist;

    va_start(vlist, name_fmt);
    task = kthread_vcreate(fn, user, name_fmt, vlist);
    va_end(vlist);

    return task;
}

void kthread_start(struct task *task)
{
    wake_up_task(task, TASK_STATE_NEW);
}

error_t kthread_run(task_fn_t fn, void *user, const char *name_fmt, ...)
{
    struct task *task;
    va_list vlist;

    va_start(vlist, name_fmt);
    task = kthread_vcreate(fn, user, name_fmt, vlist);
    va_end(vlist);

    if (unlikely(task == nullptr))
        return ENOMEM;

    kthread_start(task);
    task_unref(task);

    return EOK;
}

void kthread_exit(void)
{
    task_exit();
}
