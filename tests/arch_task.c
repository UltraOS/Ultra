#include <private/arch/task.h>
#include <private/sched/task.h>

// Userspace stand-in for the arch task switching primitives, built on ucontext
static __thread struct task *s_switched_from;

static void entry_trampoline(void)
{
    struct task *self = g_test_current;

    task_entry(s_switched_from, self->arch.fn, self->arch.user);
}

void arch_task_frame_init(struct task *task, task_fn_t fn, void *user)
{
    getcontext(&task->arch.context);

    task->arch.context.uc_stack.ss_sp = task->stack;
    task->arch.context.uc_stack.ss_size = task->stack_size;
    task->arch.context.uc_link = nullptr;
    task->arch.fn = fn;
    task->arch.user = user;

    makecontext(&task->arch.context, entry_trampoline, 0);
}

struct task *arch_switch_to(struct task *prev, struct task *next)
{
    s_switched_from = prev;
    g_test_current = next;

    swapcontext(&prev->arch.context, &next->arch.context);
    return s_switched_from;
}
