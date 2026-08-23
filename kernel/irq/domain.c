#define MSG_FMT(msg) "irq: " msg

#include <private/irq.h>

#include <bug.h>

void irq_domain_register(struct irq_domain *domain, struct irq_domain *parent)
{
    struct irq_domain *cur;
    u32 depth = 1;

    BUG_ON(domain->ops == NULL);

    domain->parent = parent;

    for (cur = parent; cur != NULL; cur = cur->parent)
        depth++;

    BUG_ON(depth > MAX_NESTED_IRQ_DOMAINS);
}
