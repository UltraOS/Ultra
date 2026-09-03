#define MSG_FMT(msg) "x86-irq: " msg

#include <arch/private/idt.h>
#include <arch/private/vectors.h>
#include <arch/registers.h>

#include <per_cpu.h>
#include <irq_helpers.h>
#include <log.h>
#include <bug.h>

static DEFINE_PER_CPU(bool, s_in_hard_irq);
static DEFINE_PER_CPU(u64, s_num_fixed_invocations[NUM_FIXED_VECTORS]);

bool in_hard_irq(void)
{
    return this_cpu_read(s_in_hard_irq);
}

/*
 * Handlers run with interrupts disabled for their entire duration,
 * so hard interrupt context can never nest. NMIs and exceptions take
 * a different path and are not part of this context.
 */
void hard_irq_enter(void)
{
    BUG_ON(this_cpu_read(s_in_hard_irq));
    this_cpu_write(s_in_hard_irq, true);
}

void hard_irq_exit(void)
{
    this_cpu_write(s_in_hard_irq, false);
}

void fixed_vector_enter(u8 vector)
{
    hard_irq_enter();
    this_cpu_inc(s_num_fixed_invocations[vector - VECTOR_FIXED_FIRST]);
}

void fixed_vector_exit(void)
{
    hard_irq_exit();
}

// No fixed vector handlers are wired up yet, every slot lands here
FIXED_VECTOR_HANDLER(X86_FIXED_UNEXPECTED, regs->interrupt_idx)
{
    pr_warn(
        "fixed vector 0x%02X fired with no handler on CPU%u\n",
        regs->interrupt_idx, unstable_cpu_id()
    );
}
