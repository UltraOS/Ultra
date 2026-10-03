#include <locking/spinlock.h>
#include <irq_helpers.h>
#include <preempt.h>

#include <test_harness.h>

TEST_CASE(spinlock_lock_unlock)
{
    struct spinlock lock;

    spin_lock_init(&lock);
    ASSERT(!spin_is_locked(&lock));

    spin_lock(&lock);
    ASSERT(spin_is_locked(&lock));
    ASSERT(!spin_try_lock(&lock));
    spin_unlock(&lock);

    ASSERT(!spin_is_locked(&lock));
    ASSERT(spin_try_lock(&lock));
    spin_unlock(&lock);
}

TEST_CASE(spinlock_static_init)
{
    DEFINE_SPINLOCK(static_lock)

    ASSERT(spin_try_lock(&static_lock));
    ASSERT(!spin_try_lock(&static_lock));
    spin_unlock(&static_lock);
}

TEST_CASE(spinlock_irq_save_restores_enabled)
{
    struct spinlock lock;
    irq_state_t state;

    spin_lock_init(&lock);
    ASSERT(local_irq_enabled());

    state = spin_lock_irq_save(&lock);
    ASSERT(!local_irq_enabled());
    spin_unlock_irq_restore(&lock, state);

    ASSERT(local_irq_enabled());
}

TEST_CASE(spinlock_irq_save_keeps_disabled)
{
    struct spinlock lock;
    irq_state_t outer, inner;

    spin_lock_init(&lock);

    outer = local_irq_state_save();
    ASSERT(!local_irq_enabled());

    inner = spin_lock_irq_save(&lock);
    ASSERT(!local_irq_enabled());
    spin_unlock_irq_restore(&lock, inner);
    ASSERT(!local_irq_enabled());

    local_irq_state_restore(outer);
    ASSERT(local_irq_enabled());
}

TEST_CASE(spinlock_try_lock_irq_save)
{
    struct spinlock lock;
    irq_state_t held, state;

    spin_lock_init(&lock);

    ASSERT(spin_try_lock_irq_save(&lock, &state));
    ASSERT(!local_irq_enabled());
    spin_unlock_irq_restore(&lock, state);
    ASSERT(local_irq_enabled());

    held = spin_lock_irq_save(&lock);
    spin_unlock(&lock);
    local_irq_state_restore(held);

    spin_lock(&lock);
    ASSERT(!spin_try_lock_irq_save(&lock, &state));
    ASSERT(local_irq_enabled());
    spin_unlock(&lock);
}

TEST_CASE(spinlock_disables_preemption)
{
    struct spinlock outer, inner;
    u32 base = preempt_count();

    spin_lock_init(&outer);
    spin_lock_init(&inner);

    spin_lock(&outer);
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(1));

    spin_lock(&inner);
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(2));

    spin_unlock(&inner);
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(1));

    spin_unlock(&outer);
    ASSERT_EQ(preempt_count(), base);
}

TEST_CASE(spinlock_irq_save_disables_preemption)
{
    struct spinlock lock;
    irq_state_t state;
    u32 base = preempt_count();

    spin_lock_init(&lock);

    state = spin_lock_irq_save(&lock);
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(1));

    spin_unlock_irq_restore(&lock, state);
    ASSERT_EQ(preempt_count(), base);
}

TEST_CASE(spinlock_try_lock_preemption)
{
    struct spinlock lock;
    irq_state_t state;
    u32 base = preempt_count();

    spin_lock_init(&lock);

    ASSERT(spin_try_lock(&lock));
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(1));

    ASSERT(!spin_try_lock(&lock));
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(1));

    ASSERT(!spin_try_lock_irq_save(&lock, &state));
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(1));

    spin_unlock(&lock);
    ASSERT_EQ(preempt_count(), base);

    ASSERT(spin_try_lock_irq_save(&lock, &state));
    ASSERT_EQ(preempt_count(), base + PREEMPT_DISABLE_DEPTH(1));

    spin_unlock_irq_restore(&lock, state);
    ASSERT_EQ(preempt_count(), base);
}
