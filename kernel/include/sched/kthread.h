#pragma once

#include <stdarg.h>

#include <common/attributes.h>
#include <common/error.h>

#include <sched/task.h>

/*
 * Create a kernel thread that runs fn(user) once it is started with
 * kthread_start(), and exits when the function returns. Use task_unref()
 * to release.
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
