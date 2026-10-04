#include <private/sched/task.h>
#include <private/arch/task.h>
#include <private/sched/core.h>

#include <common/format.h>

#include <boot/boot.h>
#include <memory/alloc.h>
#include <memory/valloc.h>
#include <memory/io.h>

#include <bug.h>
#include <refcount.h>
#include <smp.h>
#include <preempt.h>
#include <init_level.h>
#include <free_after_init.h>

DEFINE_PER_CPU(struct task, g_this_cpu_idle_task) = {
    .refcount = REFCOUNT_INIT(1),
    .name = "idle/0",
#ifndef ARCH_HAS_CUSTOM_PREEMPT_COUNT
    .preempt_count = PREEMPT_DISABLE_DEPTH(1),
#endif
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

static bool INIT_CODE is_kernel_stack(struct ultra_memory_map_entry *mme)
{
    return mme->type == ULTRA_MEMORY_TYPE_KERNEL_STACK;
}

static void INIT_CODE boot_task_set_stack(
    phys_addr_t start, phys_addr_t end, u32 type, void *user
)
{
    struct task *task = user;

    UNREFERENCED_PARAMETER(type);

    task->stack = phys_to_virt(start);
    task->stack_size = end - start;
}

/*
 * The boot CPU keeps running on the stack the loader set up, which becomes
 * the stack of its idle task. It lives in the direct map and has no guard.
 */
static error_t INIT_CODE boot_task_find_stack(void)
{
    for_each_memory_map_range(boot_task_set_stack, is_kernel_stack, current);
    BUG_ON(current->stack == nullptr);

    return EOK;
}
INIT_CALL_POST(BOOT_INFO_AVAILABLE, boot_task_find_stack);

struct task *task_alloc(void)
{
    struct task *task;

    task = alloc(sizeof(*task), ALLOC_GENERIC_ZEROED);
    if (unlikely(task == nullptr))
        return nullptr;

    task->stack = valloc(TASK_STACK_SIZE, ALLOC_GENERIC);
    if (unlikely(task->stack == nullptr)) {
        free(task);
        return nullptr;
    }

    task->stack_size = TASK_STACK_SIZE;
#ifndef ARCH_HAS_CUSTOM_PREEMPT_COUNT
    task->preempt_count = PREEMPT_COUNT_AT_SWITCH;
#endif
    refcount_set(&task->refcount, 1);
    task->state = TASK_STATE_NEW;

    return task;
}

void task_ref(struct task *task)
{
    refcount_inc(&task->refcount);
}

void task_unref(struct task *task)
{
    if (!refcount_dec_and_test(&task->refcount))
        return;

    vrelease(task->stack);
    free(task);
}

void task_entry(struct task *prev, task_fn_t fn, void *user)
{
    sched_finish_switch(prev);
    preempt_enable();

    fn(user);
    task_exit();
}
