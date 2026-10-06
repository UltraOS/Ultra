#include <sched/kthread.h>

#include <common/format.h>
#include <common/helpers.h>
#include <common/atomic.h>

#include <sched/completion.h>

#include <private/sched/task.h>
#include <private/sched/core.h>
#include <private/sched/rr.h>
#include <private/sched/kthread.h>
#include <private/arch/task.h>
#include <private/ktest/task.h>

static void kthread_entry(void *user)
{
    struct kthread *kthread = &current->kthread;

    UNREFERENCED_PARAMETER(user);

    if (kthread_should_stop())
        return;

    kthread->fn(kthread->user);
}

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
    task->flags |= TASK_FLAG_KTHREAD;
    task->kthread.fn = fn;
    task->kthread.user = user;

    completion_init(&task->kthread.exited);

    ktest_inherit_run(task);

    arch_task_frame_init(task, kthread_entry, nullptr);

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

void kthread_did_exit(struct task *task)
{
    complete_all(&task->kthread.exited);
}

void kthread_release(struct task *task)
{
    ktest_release_run(task);
}

void kthread_wait_for_exit(struct task *task)
{
    wait_for_completion(&task->kthread.exited);
}

void kthread_stop(struct task *task)
{
    task_set_flag(task, TASK_FLAG_KTHREAD_SHOULD_STOP);
    wake_up_task(task, TASK_STATE_NEW | TASK_STATE_UNINTERRUPTIBLE);
    kthread_wait_for_exit(task);

    task_unref(task);
}
