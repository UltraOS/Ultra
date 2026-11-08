#pragma once

#include <stdarg.h>

#include <common/attributes.h>
#include <common/error.h>

#include <sched/task.h>
#include <detail/sched/task.h>

/*
 * Create a kernel thread that runs fn(user) once it is started with
 * kthread_start(), and exits when the function returns. Use task_unref()
 * or kthread_stop() to release.
 */
PRINTF_DECL(3, 4)
struct task *kthread_create(
    task_fn_t fn, void *user, const char *name_fmt, ...
);

struct task *kthread_vcreate(
    task_fn_t fn, void *user, const char *name_fmt, va_list vlist
);

/*
 * Start a previously created thread.
 *
 * NOTE: the thread gets a reference of its own when started, so the caller
 *       still owns one exclusive reference at this point from the initial
 *       kthread_create() call.
 */
void kthread_start(struct task *task);

/*
 * A shorthand for kthread_create()+kthread_start(), the caller gets no
 * reference to the task.
 */
PRINTF_DECL(3, 4)
error_t kthread_run(task_fn_t fn, void *user, const char *name_fmt, ...);

// Exit the current kernel thread
NORETURN
void kthread_exit(void);

/*
 * Wait until a kernel thread that was started has exited. The caller must
 * hold a reference to the task, e.g. the one from kthread_create(), and
 * still owns it afterwards.
 */
void kthread_wait_for_exit(struct task *task);

/*
 * Same as kthread_wait_for_exit(), except the caller stops waiting once
 * 'timeout_ns' have passed. Returns true if the thread has exited, false if
 * it still hadn't when the timeout ran out.
 */
bool kthread_wait_for_exit_timeout(struct task *task, u64 timeout_ns);

/*
 * Ask a kernel thread to stop and wait until it has exited. The thread
 * has to cooperate by checking kthread_should_stop() whenever it is
 * woken.
 *
 * NOTE: This consumes the caller reference handed out by kthread_create().
 */
void kthread_stop(struct task *task);

// True once kthread_stop() asked the current thread to return
static inline bool kthread_should_stop(void)
{
    return task_test_flag(current, TASK_FLAG_KTHREAD_SHOULD_STOP);
}
