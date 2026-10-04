#include <arch/registers.h>

#include <private/arch/task.h>
#include <detail/sched/task.h>

#include <common/helpers.h>

struct x86_switch_frame {
    reg_t r15;
    reg_t r14;
    reg_t r13;
    reg_t r12;
    reg_t rbx;
    reg_t rbp;
    reg_t return_address;
};

void x86_task_entry_asm(void);

void arch_task_frame_init(struct task *task, task_fn_t fn, void *user)
{
    struct x86_switch_frame *frame;
    void *top;

    top = task->stack + task->stack_size;

    frame = top - sizeof(*frame);
    *frame = (struct x86_switch_frame) {
        .r12 = (reg_t)user,
        .rbx = (reg_t)fn,
        .return_address = (reg_t)x86_task_entry_asm,
    };

    task->arch.sp = (virt_addr_t)frame;
}
