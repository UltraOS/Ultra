#define MSG_FMT(msg) "x86-irq: " msg

#include <arch/private/idt.h>
#include <arch/private/vectors.h>
#include <arch/registers.h>

#include <private/arch/preempt.h>

#include <per_cpu.h>
#include <preempt.h>
#include <log.h>
#include <bug.h>

// Every CPU starts out with preemption disabled
DEFINE_PER_CPU(u32, g_this_cpu_preempt_count) =
    X86_PREEMPT_RESCHED_NOT_NEEDED | PREEMPT_DISABLE_DEPTH(1);

static DEFINE_PER_CPU(u64, s_num_fixed_invocations[NUM_FIXED_VECTORS]);

void fixed_vector_enter(u8 vector)
{
    hard_irq_enter();
    this_cpu_inc(s_num_fixed_invocations[vector - VECTOR_FIXED_FIRST]);
}

void fixed_vector_exit(struct registers *regs)
{
    hard_irq_exit(regs);
}

// No fixed vector handlers are wired up yet, every slot lands here
FIXED_VECTOR_HANDLER(X86_FIXED_UNEXPECTED, regs->interrupt_idx)
{
    pr_warn(
        "fixed vector 0x%02X fired with no handler on CPU%u\n",
        regs->interrupt_idx, unstable_cpu_id()
    );
}
