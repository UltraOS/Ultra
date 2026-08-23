#define MSG_FMT(msg) "irq: " msg

#include <private/irq.h>

#include <common/atomic.h>

/*
 * Runs every handler on the line with the lock dropped. The request
 * path keeps the action list untouched for the entire walk by only
 * mutating it while no walk is in flight.
 */
static bool irq_run_actions(struct irq *irq)
{
    struct irq_action *action;
    bool handled = false;

    atomic_store_relaxed(&irq->in_progress, true);
    spin_unlock(&irq->lock);

    list_for_each_entry(action, &irq->actions, node) {
        if (action->handler(action->user) == IRQ_RESULT_HANDLED)
            handled = true;
    }

    spin_lock(&irq->lock);
    atomic_store_relaxed(&irq->in_progress, false);

    return handled;
}

/*
 * Edge occurrences are transient: the controller latches the edge
 * and delivery consumes the latch. The latch is acknowledged before
 * the handlers run so that an edge arriving mid-handling is latched
 * anew and redelivered by the hardware once this delivery returns.
 * An occurrence that cannot be delivered at all is latched in
 * software instead for irq_enable() to replay.
 */
void irq_handle_edge(struct irq *irq)
{
    spin_lock(&irq->lock);

    if (irq_disabled(irq) || irq->num_actions == 0) {
        irq->state |= IRQ_STATE_MASKED | IRQ_STATE_PENDING;
        irq_hw_mask(irq);
        irq_hw_ack(irq);
        goto out;
    }

    irq_hw_ack(irq);

    if (!irq_run_actions(irq))
        irq->num_unhandled++;

out:
    spin_unlock(&irq->lock);
}

/*
 * A level line stays asserted until the device is serviced and the
 * controller's in-service gating blocks re-delivery until EOI, so
 * the happy path needs no masking. A line that cannot run its
 * handlers is masked instead, with no software latch: the
 * still-asserted line re-fires by itself once unmasked.
 */
void irq_handle_level(struct irq *irq)
{
    spin_lock(&irq->lock);

    if (irq_disabled(irq) || irq->num_actions == 0) {
        irq->state |= IRQ_STATE_MASKED;
        irq_hw_mask(irq);
        goto out_eoi;
    }

    if (!irq_run_actions(irq))
        irq->num_unhandled++;

out_eoi:
    irq_hw_eoi(irq);
    spin_unlock(&irq->lock);
}

void irq_deliver(struct irq *irq)
{
    irq->flow(irq);
}
