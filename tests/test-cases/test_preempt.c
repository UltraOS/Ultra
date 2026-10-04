#include <preempt.h>
#include <private/sched/task.h>
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

TEST_CASE(preempt_reaching_zero_alone_does_not_reschedule)
{
    u32 base;

    base = preempt_count();
    preempt_count_add(PREEMPT_DISABLE_UNIT);
    ASSERT(!preempt_count_sub_and_test(PREEMPT_DISABLE_UNIT));
    ASSERT(!preempt_should_resched());
    ASSERT_EQ(preempt_count(), base);
}

TEST_CASE(preempt_need_resched_takes_effect_at_zero)
{
    u32 base;

    base = preempt_count();
    preempt_count_add(PREEMPT_DISABLE_DEPTH(2));
    task_test_and_set_flag(current, TASK_FLAG_NEED_RESCHED);
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(2));
    ASSERT(!preempt_should_resched());

    ASSERT(!preempt_count_sub_and_test(PREEMPT_DISABLE_UNIT));
    ASSERT(preempt_count_sub_and_test(PREEMPT_DISABLE_UNIT));
    ASSERT(preempt_should_resched());

    task_clear_flag(current, TASK_FLAG_NEED_RESCHED);
    ASSERT(!preempt_should_resched());
    ASSERT_EQ(preempt_count(), base);
}
