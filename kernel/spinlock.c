#include <common/atomic.h>
#include <arch/cpu_helpers.h>

#include <spinlock.h>
#include <irq_helpers.h>

#define CURRENT_TICKET_SHIFT 16
#define LAST_FREED_TICKET_SHIFT 0
#define TICKET_MASK 0xFFFF

#define CURRENT_TICKET(ctrl) \
    (((ctrl) >> CURRENT_TICKET_SHIFT) & TICKET_MASK)
#define LAST_FREED_TICKET(ctrl) \
    ((((ctrl) >> LAST_FREED_TICKET_SHIFT)) & TICKET_MASK)

void spin_lock(struct spinlock *lock)
{
    u32 ctrl;
    u16 my_ticket;

    ctrl = atomic_fetch_add(
        &lock->control, 1u << CURRENT_TICKET_SHIFT, MO_ACQ_REL
    );
    my_ticket = CURRENT_TICKET(ctrl);

    // Fast path if the lock was already free
    if (LAST_FREED_TICKET(ctrl) == my_ticket)
        return;

    do {
        arch_cpu_relax();
        ctrl = atomic_load_acquire(&lock->control);
    } while (LAST_FREED_TICKET(ctrl) != my_ticket);
}

irq_state_t spin_lock_irq_save(struct spinlock *lock)
{
    irq_state_t irq_state;

    irq_state = irq_state_save();
    spin_lock(lock);

    return irq_state;
}

bool spin_try_lock(struct spinlock *lock)
{
    u32 ctrl;

    ctrl = atomic_load_acquire(&lock->control);
    if (CURRENT_TICKET(ctrl) != LAST_FREED_TICKET(ctrl))
        return false;

    return atomic_cmpxchg_acq_rel(
        &lock->control, &ctrl, ctrl + (1u << CURRENT_TICKET_SHIFT)
    );
}

bool spin_try_lock_irq_save(struct spinlock *lock, irq_state_t *out_irq_state)
{
    irq_state_t irq_state;

    irq_state = irq_state_save();
    if (spin_try_lock(lock)) {
        *out_irq_state = irq_state;
        return true;
    }

    irq_state_restore(irq_state);
    return false;
}

void spin_unlock(struct spinlock *lock)
{
    u32 ctrl;

    ctrl = atomic_load_relaxed(&lock->control);
    atomic_store_release(&lock->last_freed_ticket, LAST_FREED_TICKET(ctrl) + 1);
}

void spin_unlock_irq_restore(struct spinlock *lock, irq_state_t irq_state)
{
    spin_unlock(lock);
    irq_state_restore(irq_state);
}

bool spin_is_locked(struct spinlock *lock)
{
    u32 ctrl;

    ctrl = atomic_load_relaxed(&lock->control);
    return CURRENT_TICKET(ctrl) > LAST_FREED_TICKET(ctrl);
}

bool spin_is_contended(struct spinlock *lock)
{
    u32 ctrl;
    u16 ticket_gap;

    ctrl = atomic_load_relaxed(&lock->control);
    ticket_gap = CURRENT_TICKET(ctrl) - LAST_FREED_TICKET(ctrl);

    /*
     * A ticket gap of 1 just means the lock is currently busy, but there is no
     * contention. Anything above means N - 1 CPUs waiting in line.
     */
    return ticket_gap >= 2;
}
