#include <preempt.h>
#include <private/arch/preempt.h>

#include <test_harness.h>

TEST_CASE(preempt_disable_nests)
{
    u32 base = preempt_count();

    preempt_disable();
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(1));

    preempt_disable();
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(2));

    preempt_enable();
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(1));

    preempt_enable();
    ASSERT_EQ(preempt_count(), base);
}

TEST_CASE(preempt_hard_irq_context)
{
    u32 base = preempt_count();

    ASSERT(!in_hard_irq());

    hard_irq_enter();
    ASSERT(in_hard_irq());
    ASSERT_EQ(preempt_count(), base + PREEMPT_HARD_IRQ_DEPTH(1));

    hard_irq_exit();
    ASSERT(!in_hard_irq());
    ASSERT_EQ(preempt_count(), base);
}

TEST_CASE(preempt_hard_irq_independent_of_depth)
{
    u32 base = preempt_count();

    preempt_disable();
    ASSERT(!in_hard_irq());

    hard_irq_enter();
    preempt_disable();
    ASSERT(in_hard_irq());
    ASSERT_EQ(
        preempt_count(),
        base + PREEMPT_DISABLE_DEPTH(2) + PREEMPT_HARD_IRQ_DEPTH(1)
    );
    preempt_enable();
    hard_irq_exit();

    ASSERT(!in_hard_irq());
    preempt_enable();
    ASSERT_EQ(preempt_count(), base);
}
