#define MSG_FMT(msg) "lapic: " msg

#include <arch/private/idt.h>
#include <arch/private/irq.h>
#include <arch/private/vectors.h>
#include <arch/private/vector_alloc.h>
#include <arch/private/apic.h>
#include <arch/private/smp.h>
#include <arch/registers.h>
#include <arch/smp.h>

#include <private/irq.h>

#include <per_cpu.h>
#include <init_level.h>
#include <free_after_init.h>
#include <memory/alloc.h>
#include <log.h>
#include <bug.h>

static DEFINE_PER_CPU(struct irq*, s_dynamic_vectors[NUM_DYNAMIC_VECTORS]);
static DEFINE_PER_CPU(u64, s_num_unexpected);

static struct irq **dynamic_vector_slot(u32 cpu, u8 vector)
{
    BUG_ON(vector < VECTOR_DYNAMIC_FIRST || vector > VECTOR_DYNAMIC_LAST);
    return &per_cpu(s_dynamic_vectors[vector - VECTOR_DYNAMIC_FIRST], cpu);
}

IRQ_HANDLER {
    struct irq *irq;
    u8 vector = regs->interrupt_idx;

    hard_irq_enter();

    irq = this_cpu_read(s_dynamic_vectors[vector - VECTOR_DYNAMIC_FIRST]);
    if (irq == NULL) {
        this_cpu_inc(s_num_unexpected);
        pr_warn(
            "unexpected vector 0x%02X on CPU%u\n",
            vector, unstable_cpu_id()
        );

        /*
         * A set in-service bit that never gets an EOI blocks its
         * entire priority class forever.
         */
        if (apic_vector_in_isr(vector))
            apic_eoi();

        goto out;
    }

    irq_deliver(irq);

out:
    hard_irq_exit();
}

// The domain's private bookkeeping for one allocated interrupt
struct lapic_route {
    u32 cpu;
    u32 dest_apic_id;
    u8 vector;
};

static void lapic_eoi(struct irq_level *level)
{
    UNREFERENCED_PARAMETER(level);
    apic_eoi();
}

static error_t lapic_retrigger(struct irq_level *level)
{
    struct lapic_route *route = level->chip_data;

    if (route->cpu == unstable_cpu_id()) {
        apic_send_ipi_self(route->vector);
        return EOK;
    }

    apic_send_fixed_ipi(route->dest_apic_id, route->vector);
    return EOK;
}

// Trigger bits are the consumer's, only the route is composed here
static void lapic_compose_msi_route(
    struct irq_level *level, struct msi_route_msg *out
)
{
    struct lapic_route *route = level->chip_data;

    out->data = route->vector;
    out->address_high = 0;

    out->address_low = X86_MSI_ADDR_BASE;
    out->address_low |= BIT_FIELD_MAKE(
        X86_MSI_ADDR_DESTID_0_7_MASK, route->dest_apic_id
    );

    /*
     * We filter APICs outside of the addressable range early so the
     * destination id >> 8 is guaranteed to be zero if bits 14..8 are
     * not supported.
     */
    out->address_low |= BIT_FIELD_MAKE(
        X86_MSI_ADDR_VIRT_DESTID_8_14_MASK, route->dest_apic_id >> 8
    );
}

static const struct irq_chip s_lapic_chip = {
    .name = "lapic",
    .ack = lapic_eoi,
    .eoi = lapic_eoi,
    .retrigger = lapic_retrigger,
    .compose_msi_route = lapic_compose_msi_route,
};

static error_t lapic_domain_alloc(
    struct irq *irq, struct irq_level *level, struct irq_alloc_request *desc
)
{
    struct lapic_route *route;
    error_t ret;

    UNREFERENCED_PARAMETER(irq);

    route = alloc(sizeof(*route), ALLOC_GENERIC_ZEROED);
    if (route == NULL)
        return ENOMEM;

    ret = vector_alloc(desc->affinity, &route->cpu, &route->vector);
    if (is_error(ret)) {
        free(route);
        return ret;
    }

    route->dest_apic_id = per_cpu(g_this_cpu_apic_id, route->cpu);

    level->chip = &s_lapic_chip;
    level->chip_data = route;
    level->line = route->vector;
    return EOK;
}

static void lapic_domain_free(struct irq *irq, struct irq_level *level)
{
    struct lapic_route *route = level->chip_data;

    UNREFERENCED_PARAMETER(irq);

    vector_free(route->cpu, route->vector);
    free(route);
}

static error_t lapic_domain_activate(struct irq *irq, struct irq_level *level)
{
    struct lapic_route *route = level->chip_data;
    struct irq **slot;

    slot = dynamic_vector_slot(route->cpu, route->vector);
    BUG_ON(*slot != NULL);

    *slot = irq;
    return EOK;
}

static void lapic_domain_deactivate(struct irq *irq, struct irq_level *level)
{
    struct lapic_route *route = level->chip_data;
    struct irq **slot;

    UNREFERENCED_PARAMETER(irq);

    slot = dynamic_vector_slot(route->cpu, route->vector);
    BUG_ON(*slot == NULL);

    *slot = NULL;
}

static const struct irq_domain_ops s_lapic_domain_ops = {
    .alloc = lapic_domain_alloc,
    .free = lapic_domain_free,
    .activate = lapic_domain_activate,
    .deactivate = lapic_domain_deactivate,
};

struct irq_domain g_x86_lapic_domain = {
    .name = "lapic",
    .ops = &s_lapic_domain_ops,
};

/*
 * The root domain registering is what makes interrupts requestable,
 * so it is the establisher of IRQS_AVAILABLE on x86.
 */
static error_t INIT_CODE x86_irqs_init(void)
{
    irq_domain_register(&g_x86_lapic_domain, NULL);
    return EOK;
}
INIT_CALL_AT(IRQS_AVAILABLE, x86_irqs_init);
