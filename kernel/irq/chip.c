#define MSG_FMT(msg) "irq: " msg

#include <private/irq.h>

#include <arch/cpu_helpers.h>

void irq_hw_mask(struct irq *irq)
{
    u32 i;

    for (i = 0; i < irq->num_levels; i++) {
        const struct irq_chip *chip = irq->levels[i].chip;

        if (chip->mask == NULL)
            continue;

        chip->mask(&irq->levels[i]);
        return;
    }
}

void irq_hw_unmask(struct irq *irq)
{
    u32 i;

    for (i = 0; i < irq->num_levels; i++) {
        const struct irq_chip *chip = irq->levels[i].chip;

        if (chip->unmask == NULL)
            continue;

        chip->unmask(&irq->levels[i]);
        return;
    }
}

error_t irq_hw_retrigger(struct irq *irq)
{
    u32 i;

    for (i = 0; i < irq->num_levels; i++) {
        const struct irq_chip *chip = irq->levels[i].chip;

        if (chip->retrigger == NULL)
            continue;

        return chip->retrigger(&irq->levels[i]);
    }

    return ENOTSUP;
}

void irq_hw_ack(struct irq *irq)
{
    u32 i;

    for (i = 0; i < irq->num_levels; i++) {
        const struct irq_chip *chip = irq->levels[i].chip;

        if (chip->ack == NULL)
            continue;

        chip->ack(&irq->levels[i]);
    }
}

void irq_hw_eoi(struct irq *irq)
{
    u32 i;

    for (i = 0; i < irq->num_levels; i++) {
        const struct irq_chip *chip = irq->levels[i].chip;

        if (chip->eoi == NULL)
            continue;

        chip->eoi(&irq->levels[i]);
    }
}

bool irq_hw_is_outstanding(struct irq *irq)
{
    u32 i;

    for (i = 0; i < irq->num_levels; i++) {
        const struct irq_chip *chip = irq->levels[i].chip;

        if (chip->is_outstanding == NULL)
            continue;
        if (chip->is_outstanding(&irq->levels[i]))
            return true;
    }

    return false;
}

void irq_hw_drain(struct irq *irq)
{
    while (irq_hw_is_outstanding(irq))
        arch_cpu_relax();
}
