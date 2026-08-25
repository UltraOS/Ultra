#define MSG_FMT(msg) "ioapic: " msg

#include <arch/private/ioapic.h>
#include <arch/private/irq.h>
#include <arch/private/apic.h>
#include <arch/irq.h>
#include <arch/constants.h>

#include <private/irq.h>

#include <log.h>
#include <bug.h>
#include <init_level.h>
#include <free_after_init.h>
#include <spinlock.h>

#include <memory/io.h>
#include <memory/alloc.h>

#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#include <common/string.h>
#include <common/format.h>
#include <common/bit.h>

#define IOAPIC_IOREGSEL 0x00
#define IOAPIC_IOWIN 0x10
#define IOAPIC_EOIR 0x40

enum ioapic_reg {
    IOAPIC_REG_ID = 0x00,
        #define IOAPIC_ID MAKE_BIT_MASK(31, 24)

    IOAPIC_REG_VER = 0x01,
        #define IOAPIC_VERSION MAKE_BIT_MASK(7, 0)
        #define IOAPIC_MAX_REDIR_ENTRY MAKE_BIT_MASK(23, 16)

        // The directed EOI register only exists from this version on
        #define IOAPIC_MIN_VERSION_EOIR 0x20

    IOAPIC_REG_ARB = 0x02,
    IOAPIC_REG_IOREDTBL = 0x10,
        // Two registers per entry, the low word first
        #define IOAPIC_RTE_STRIDE 2
        #define IOAPIC_RTE_VECTOR_MASK MAKE_BIT_MASK_U32(7, 0)
        #define IOAPIC_RTE_DELIVERY_MASK MAKE_BIT_MASK_U32(10, 8)
        #define IOAPIC_RTE_DELIVERY_SMI 0x2
        #define IOAPIC_RTE_DEST_MODE_LOGICAL BIT_U32(11)
        #define IOAPIC_RTE_ACTIVE_LOW BIT_U32(13)
        #define IOAPIC_RTE_REMOTE_IRR BIT_U32(14)
        #define IOAPIC_RTE_LEVEL BIT_U32(15)
        #define IOAPIC_RTE_MASKED BIT_U32(16)

        // The high half of an entry
        #define IOAPIC_RTE_VIRT_DESTID_8_14_MASK MAKE_BIT_MASK_U32(23, 17)
        #define IOAPIC_RTE_DESTID_0_7_MASK MAKE_BIT_MASK_U32(31, 24)
};

struct ioapic {
    u8 id;
    u8 version;
    phys_addr_t base;
    io_window iow;
    u32 gsi_base, gsi_last;

    // Guards the two-step index/data register window
    struct spinlock lock;
    struct irq_domain domain;
    char name[16];
};

#define MAX_IOAPICS 128
#define IOAPIC_IDX_UNKNOWN 0xFF

static struct ioapic s_ioapics[MAX_IOAPICS];
static size_t s_num_ioapics;

static u32 ioapic_num_pins(struct ioapic *ioapic)
{
    return ioapic->gsi_last - ioapic->gsi_base + 1;
}

static struct ioapic *find_ioapic_for_gsi(u32 gsi)
{
    size_t i;

    for (i = 0; i < s_num_ioapics; i++) {
        if (s_ioapics[i].gsi_base <= gsi && gsi <= s_ioapics[i].gsi_last)
            return &s_ioapics[i];
    }

    return nullptr;
}

struct isa_irq_source {
    u32 gsi;
    u8 polarity;
    u8 triggering;
    u8 ioapic_idx;
    u8 ioapic_pin;
    bool present;
};

#define DEFAULT_ISA_SOURCE(idx) {               \
    .gsi = (idx),                               \
    .polarity = ACPI_MADT_POLARITY_ACTIVE_HIGH, \
    .triggering = ACPI_MADT_TRIGGERING_EDGE,    \
    .ioapic_idx = IOAPIC_IDX_UNKNOWN,           \
    .present = true                             \
}

static struct isa_irq_source s_isa_irq_sources[NUM_ISA_IRQS] = {
    DEFAULT_ISA_SOURCE(0),
    DEFAULT_ISA_SOURCE(1),
    DEFAULT_ISA_SOURCE(2),
    DEFAULT_ISA_SOURCE(3),
    DEFAULT_ISA_SOURCE(4),
    DEFAULT_ISA_SOURCE(5),
    DEFAULT_ISA_SOURCE(6),
    DEFAULT_ISA_SOURCE(7),
    DEFAULT_ISA_SOURCE(8),
    DEFAULT_ISA_SOURCE(9),
    DEFAULT_ISA_SOURCE(10),
    DEFAULT_ISA_SOURCE(11),
    DEFAULT_ISA_SOURCE(12),
    DEFAULT_ISA_SOURCE(13),
    DEFAULT_ISA_SOURCE(14),
    DEFAULT_ISA_SOURCE(15),
};

static INIT_CODE void irq_resolve_activation(
    u8 irq, u8 *in_out_polarity, u8 *in_out_triggering
)
{
    struct acpi_fadt *fadt;

    BUG_ON(uacpi_table_fadt(&fadt) != UACPI_STATUS_OK);

    switch (*in_out_polarity) {
    case ACPI_MADT_POLARITY_ACTIVE_LOW:
    case ACPI_MADT_POLARITY_ACTIVE_HIGH:
        break;
    default:
        pr_warn(
            "invalid polarity value %d for irq %d, assuming active high\n",
            *in_out_polarity, irq
        );
        FALLTHROUGH;
    case ACPI_MADT_POLARITY_CONFORMING:
        // SCI is active low by default
        if (irq == fadt->sci_int)
            *in_out_polarity = ACPI_MADT_POLARITY_ACTIVE_LOW;
        else // ISA is always active high
            *in_out_polarity = ACPI_MADT_POLARITY_ACTIVE_HIGH;
        break;
    }

    switch (*in_out_triggering) {
    case ACPI_MADT_TRIGGERING_LEVEL:
        if (unlikely(irq == 0)) {
            pr_warn("timer IRQ claims level triggering, using edge anyway\n");
            *in_out_triggering = ACPI_MADT_TRIGGERING_EDGE;
        }
        break;
    case ACPI_MADT_TRIGGERING_EDGE:
        break;
    default:
        pr_warn(
            "invalid triggering value %d for irq %d, assuming edge\n",
            *in_out_triggering, irq
        );
        FALLTHROUGH;
    case ACPI_MADT_TRIGGERING_CONFORMING:
        // SCI is level triggered by default
        if (irq == fadt->sci_int)
            *in_out_triggering = ACPI_MADT_TRIGGERING_LEVEL;
        else // ISA is always edge-triggered
            *in_out_triggering = ACPI_MADT_TRIGGERING_EDGE;
    }
}

void INIT_CODE ioapic_register_isa_irq_override(
    u8 irq, u32 gsi, u8 polarity, u8 triggering
)
{
    struct ioapic *owner;

    if (WARN_ON(irq >= NUM_ISA_IRQS))
        return;

    owner = find_ioapic_for_gsi(gsi);
    if (unlikely(owner == nullptr)) {
        pr_warn(
            "unable to register IRQ override %u->%u: "
            "no IOAPIC covers this GSI\n", irq, gsi
        );
        return;
    }

    irq_resolve_activation(irq, &polarity, &triggering);

    s_isa_irq_sources[irq] = (struct isa_irq_source) {
        .gsi = gsi,
        .polarity = polarity,
        .triggering = triggering,
        .ioapic_idx = owner - s_ioapics,
        .ioapic_pin = gsi - owner->gsi_base,
        .present = true,
    };
    pr_info(
        "registered IRQ override %u->%u %s/%s\n", irq, gsi,
        polarity == ACPI_MADT_POLARITY_ACTIVE_LOW ? "low" : "high",
        triggering == ACPI_MADT_TRIGGERING_EDGE ? "edge" : "level"
    );

    /*
     * This is not an identity IRQ->GSI mapping that only changes the
     * polarity/triggering flags. We must take that into account and
     * destroy an existing predefined identity mapping that this GSI
     * is linked to.
     */
    if (irq != gsi && gsi < NUM_ISA_IRQS && s_isa_irq_sources[gsi].gsi == gsi)
        s_isa_irq_sources[gsi].present = false;
}

void INIT_CODE ioapic_finalize_overrides(void)
{
    u32 i;
    struct ioapic *owner;
    struct acpi_fadt *fadt;
    struct isa_irq_source *source;

    BUG_ON(uacpi_table_fadt(&fadt) != UACPI_STATUS_OK);

    for (i = 0; i < NUM_ISA_IRQS; i++) {
        source = &s_isa_irq_sources[i];

        if (!source->present) {
            pr_debug("IRQ %u is not mapped to any GSI\n", i);
            continue;
        }

        // This IRQ had an explicit override provided, skip
        if (source->ioapic_idx != IOAPIC_IDX_UNKNOWN)
            continue;

        /*
         * This should be an identity map that we set up at compile time,
         * otherwise we expect that it has a valid IOAPIC idx set.
         */
        BUG_ON(source->gsi != i);

        owner = find_ioapic_for_gsi(i);
        if (unlikely(owner == nullptr)) {
            pr_debug("no IOAPIC covers ISA IRQ %u\n", i);
            source->present = false;
            continue;
        }

        source->ioapic_idx = owner - s_ioapics;
        source->ioapic_pin = i - owner->gsi_base;

        /*
         * SCI default polarity and triggering is different from ISA's. Manually
         * override the values here if MADT didn't bother to specify it.
         */
        if (i == fadt->sci_int) {
            source->polarity = ACPI_MADT_POLARITY_CONFORMING;
            source->triggering = ACPI_MADT_TRIGGERING_CONFORMING;
            irq_resolve_activation(i, &source->polarity, &source->triggering);
        }
    }
}

static INIT_CODE bool ioapic_check_collisions(
    struct ioapic *ioapic
)
{
    size_t i;
    const char *which;
    struct ioapic *other;

    for (i = 0; i < s_num_ioapics; i++) {
        other = &s_ioapics[i];

        if (unlikely(ioapic->base == other->base)) {
            which = "address";
            goto out_collision;
        }

        if (unlikely(ioapic->gsi_base <= other->gsi_last &&
                     other->gsi_base <= ioapic->gsi_last)) {
            which = "GSI range";
            goto out_collision;
        }
    }

    return false;

out_collision:
    pr_warn(
        "unable to register IOAPIC[%u] (0x%llX, GSI[%u->%u]): "
        "%s collides with existing IOAPIC[%u] (0x%llX, GSI[%u->%u])\n",
        ioapic->id, ioapic->base, ioapic->gsi_base, ioapic->gsi_last, which,
        other->id, other->base, other->gsi_base, other->gsi_last
    );
    return true;
}

static struct ioapic* INIT_CODE ioapic_next_empty_slot(void)
{
    if (s_num_ioapics >= MAX_IOAPICS)
        return nullptr;

    return &s_ioapics[s_num_ioapics];
}

static u32 ioapic_read(struct ioapic *ioapic, enum ioapic_reg reg)
{
    u32 value;

    iowrite32(&ioapic->iow, IOAPIC_IOREGSEL, (u32)reg);
    value = ioread32(&ioapic->iow, IOAPIC_IOWIN);

    return value;
}

static void ioapic_write(struct ioapic *ioapic, enum ioapic_reg reg, u32 value)
{
    iowrite32(&ioapic->iow, IOAPIC_IOREGSEL, (u32)reg);
    iowrite32(&ioapic->iow, IOAPIC_IOWIN, value);
}

/*
 * IOAPIC register writes are posted, but for some writes (namely masking an
 * RTE), it's very important that the write completes before returning back.
 * We accomplish this by simply reading back what we wrote.
 */
static void ioapic_write_sync(
    struct ioapic *ioapic, enum ioapic_reg reg, u32 value
)
{
    ioapic_write(ioapic, reg, value);
    ioread32(&ioapic->iow, IOAPIC_IOWIN);
}

static enum ioapic_reg ioapic_rte_low_reg(u32 pin)
{
    return IOAPIC_REG_IOREDTBL + pin * IOAPIC_RTE_STRIDE;
}

static enum ioapic_reg ioapic_rte_high_reg(u32 pin)
{
    return ioapic_rte_low_reg(pin) + 1;
}

/*
 * Clear a level pin's Remote IRR without the EOI broadcast, the
 * caller holds the lock.
 */
static void ioapic_pin_eoi(struct ioapic *ioapic, u32 pin, u8 vector)
{
    enum ioapic_reg reg;
    u32 value;

    if (ioapic->version >= IOAPIC_MIN_VERSION_EOIR) {
        iowrite32(&ioapic->iow, IOAPIC_EOIR, vector);
        return;
    }

    /*
     * No EOI register: flipping the masked entry to edge and back
     * drops the latch instead.
     */
    reg = ioapic_rte_low_reg(pin);
    value = ioapic_read(ioapic, reg);
    ioapic_write(
        ioapic, reg, (value | IOAPIC_RTE_MASKED) & ~IOAPIC_RTE_LEVEL
    );
    ioapic_write(ioapic, reg, value);
}

// The domain's private bookkeeping for one allocated pin
struct ioapic_route {
    struct ioapic *ioapic;

    // The low word of the entry as last written, mask bit included
    u32 rte_low;
};

static void ioapic_pin_set_masked(struct irq_level *level, bool masked)
{
    struct ioapic_route *route = level->chip_data;
    struct ioapic *ioapic = route->ioapic;
    enum ioapic_reg reg;
    irq_state_t irq_state;

    reg = ioapic_rte_low_reg(level->line);

    irq_state = spin_lock_irq_save(&ioapic->lock);

    if (masked) {
        route->rte_low |= IOAPIC_RTE_MASKED;
        // Masking must be done synchronously
        ioapic_write_sync(ioapic, reg, route->rte_low);
    } else {
        route->rte_low &= ~IOAPIC_RTE_MASKED;
        ioapic_write(ioapic, reg, route->rte_low);
    }

    spin_unlock_irq_restore(&ioapic->lock, irq_state);
}

static void ioapic_chip_mask(struct irq_level *level)
{
    ioapic_pin_set_masked(level, true);
}

static void ioapic_chip_unmask(struct irq_level *level)
{
    ioapic_pin_set_masked(level, false);
}

static void ioapic_chip_eoi(struct irq_level *level)
{
    struct ioapic_route *route = level->chip_data;
    struct ioapic *ioapic = route->ioapic;
    irq_state_t irq_state;
    u8 vector;

    vector = route->rte_low & IOAPIC_RTE_VECTOR_MASK;
    if (likely(apic_vector_in_tmr(vector)))
        /*
         * LAPIC agrees that this IRQ is level triggered, so nothing is needed
         * on our side.
         */
        return;

    /*
     * IOAPIC decided to send this IRQ as edge triggered, so LAPIC EOI
     * won't clear it, we must do it ourselves. If we don't, Remote IRR
     * will remain set so this pin will remain blocked forever.
     */
    irq_state = spin_lock_irq_save(&ioapic->lock);
    ioapic_pin_eoi(ioapic, level->line, vector);
    spin_unlock_irq_restore(&ioapic->lock, irq_state);
}

/*
 * A level occurrence the target CPU has accepted but not yet serviced
 * holds Remote IRR, and only an EOI arriving while the entry is still
 * level with the same vector releases it. The bit means nothing for
 * an edge entry.
 */
static bool ioapic_chip_is_outstanding(struct irq_level *level)
{
    struct ioapic_route *route = level->chip_data;
    struct ioapic *ioapic = route->ioapic;
    irq_state_t irq_state;
    u32 value;

    irq_state = spin_lock_irq_save(&ioapic->lock);
    value = ioapic_read(ioapic, ioapic_rte_low_reg(level->line));
    spin_unlock_irq_restore(&ioapic->lock, irq_state);

    if (!(value & IOAPIC_RTE_LEVEL))
        return false;

    return value & IOAPIC_RTE_REMOTE_IRR;
}

// ack/retrigger are handled by LAPIC
static const struct irq_chip s_ioapic_chip = {
    .name = "ioapic",
    .mask = ioapic_chip_mask,
    .unmask = ioapic_chip_unmask,
    .eoi = ioapic_chip_eoi,
    .is_outstanding = ioapic_chip_is_outstanding,
};

static error_t ioapic_domain_alloc(
    struct irq *irq, struct irq_level *level, struct irq_alloc_request *desc
)
{
    struct ioapic *ioapic = level->domain->priv;
    struct ioapic_route *route;
    irq_line_t pin = desc->spec.line;

    UNREFERENCED_PARAMETER(irq);

    // A pin latches one edge, the GPIO-world trigger cannot be served
    if (desc->spec.trigger == IRQ_TRIGGER_EDGE_BOTH)
        return EINVAL;

    if (pin >= ioapic_num_pins(ioapic))
        return EINVAL;

    route = alloc(sizeof(*route), ALLOC_GENERIC_ZEROED);
    if (route == NULL)
        return ENOMEM;

    route->ioapic = ioapic;

    level->chip = &s_ioapic_chip;
    level->chip_data = route;
    level->line = pin;
    return EOK;
}

static void ioapic_domain_free(struct irq *irq, struct irq_level *level)
{
    UNREFERENCED_PARAMETER(irq);
    free(level->chip_data);
}

static error_t ioapic_domain_activate(struct irq *irq, struct irq_level *level)
{
    struct ioapic_route *route = level->chip_data;
    struct ioapic *ioapic = route->ioapic;
    struct msi_route_msg msg;
    irq_state_t irq_state;
    u32 low, high, value;

    irq_hw_compose_msi_route(irq, &msg);

    /*
     * An RTE is the composed message in a different register layout,
     * moved field by field since the bit positions differ. The
     * vector and delivery mode occupy the same bits on both sides.
     */
    low = msg.data & (X86_MSI_DATA_VECTOR_MASK | X86_MSI_DATA_DELIVERY_MASK);
    if (msg.address_low & X86_MSI_ADDR_DEST_MODE_LOGICAL)
        low |= IOAPIC_RTE_DEST_MODE_LOGICAL;

    value = BIT_FIELD_READ(msg.address_low, X86_MSI_ADDR_DESTID_0_7_MASK);
    high = BIT_FIELD_MAKE(IOAPIC_RTE_DESTID_0_7_MASK, value);

    value = BIT_FIELD_READ(
        msg.address_low, X86_MSI_ADDR_VIRT_DESTID_8_14_MASK
    );
    high |= BIT_FIELD_MAKE(IOAPIC_RTE_VIRT_DESTID_8_14_MASK, value);

    // Trigger facts are the pin's own, layered on top of the message
    if (irq_trigger_is_level(irq->spec.trigger))
        low |= IOAPIC_RTE_LEVEL;
    if (irq_trigger_is_active_low(irq->spec.trigger))
        low |= IOAPIC_RTE_ACTIVE_LOW;

    // .activate() expects the line to remain masked until a later .unmask()
    low |= IOAPIC_RTE_MASKED;

    irq_state = spin_lock_irq_save(&ioapic->lock);
    route->rte_low = low;
    ioapic_write(ioapic, ioapic_rte_high_reg(level->line), high);
    ioapic_write(ioapic, ioapic_rte_low_reg(level->line), low);
    spin_unlock_irq_restore(&ioapic->lock, irq_state);

    return EOK;
}

static void ioapic_domain_deactivate(
    struct irq *irq, struct irq_level *level
)
{
    struct ioapic_route *route = level->chip_data;
    struct ioapic *ioapic = route->ioapic;
    irq_state_t irq_state;

    UNREFERENCED_PARAMETER(irq);

    irq_state = spin_lock_irq_save(&ioapic->lock);

    // The mask bit must be in place before the route is wiped
    ioapic_write(ioapic, ioapic_rte_low_reg(level->line), IOAPIC_RTE_MASKED);
    ioapic_write(ioapic, ioapic_rte_high_reg(level->line), 0);

    spin_unlock_irq_restore(&ioapic->lock, irq_state);
}

static const struct irq_domain_ops s_ioapic_domain_ops = {
    .alloc = ioapic_domain_alloc,
    .free = ioapic_domain_free,
    .activate = ioapic_domain_activate,
    .deactivate = ioapic_domain_deactivate,
};

static enum irq_trigger isa_source_trigger(struct isa_irq_source *source)
{
    bool low;

    low = source->polarity == ACPI_MADT_POLARITY_ACTIVE_LOW;

    if (source->triggering == ACPI_MADT_TRIGGERING_LEVEL) {
        return low ? IRQ_TRIGGER_LEVEL_ACTIVE_LOW :
                     IRQ_TRIGGER_LEVEL_ACTIVE_HIGH;
    }

    return low ? IRQ_TRIGGER_EDGE_ACTIVE_LOW : IRQ_TRIGGER_EDGE_ACTIVE_HIGH;
}

error_t isa_irq_get(u8 isa_irq, struct irq_spec *out)
{
    struct isa_irq_source *source;
    error_t ret;

    if (WARN_ON(isa_irq >= NUM_ISA_IRQS))
        return EINVAL;

    // Unmapped lines were already reported by the finalize pass
    source = &s_isa_irq_sources[isa_irq];
    if (!source->present)
        return ENOENT;

    // A present source is guaranteed to be covered by an IOAPIC
    ret = ioapic_gsi_to_pin(source->gsi, &out->domain, &out->line);
    if (WARN_ON(is_error(ret)))
        return ret;

    out->trigger = isa_source_trigger(source);
    return EOK;
}

error_t ioapic_gsi_to_pin(
    u32 gsi, struct irq_domain **out_domain, irq_line_t *out_pin
)
{
    struct ioapic *ioapic;

    ioapic = find_ioapic_for_gsi(gsi);
    if (ioapic == NULL)
        return ENOENT;

    *out_domain = &ioapic->domain;
    *out_pin = gsi - ioapic->gsi_base;
    return EOK;
}

static void INIT_CODE ioapic_quiesce_pin(struct ioapic *ioapic, u32 pin)
{
    enum ioapic_reg reg;
    u32 value;

    reg = ioapic_rte_low_reg(pin);
    value = ioapic_read(ioapic, reg);

    // Firmware may route SMI through a pin, that must keep working
    if (BIT_FIELD_READ(value, IOAPIC_RTE_DELIVERY_MASK) ==
        IOAPIC_RTE_DELIVERY_SMI)
        return;

    /*
     * Remote IRR may still change until the mask lands, the entry
     * read back afterwards is the settled one.
     */
    if (!(value & IOAPIC_RTE_MASKED)) {
        ioapic_write(ioapic, reg, value | IOAPIC_RTE_MASKED);
        value = ioapic_read(ioapic, reg);
    }

    /*
     * A Remote IRR left set by an interrupt the firmware never
     * acknowledged blocks the pin forever. An explicit EOI only
     * clears it in level mode, so force that first.
     */
    if (value & IOAPIC_RTE_REMOTE_IRR) {
        if (!(value & IOAPIC_RTE_LEVEL)) {
            value |= IOAPIC_RTE_LEVEL;
            ioapic_write(ioapic, reg, value);
        }

        ioapic_pin_eoi(ioapic, pin, value & IOAPIC_RTE_VECTOR_MASK);
    }

    ioapic_write(ioapic, reg, IOAPIC_RTE_MASKED);
    ioapic_write(ioapic, ioapic_rte_high_reg(pin), 0);

    value = ioapic_read(ioapic, reg);
    if (unlikely(value & IOAPIC_RTE_REMOTE_IRR)) {
        pr_warn(
            "IOAPIC[%u] pin %u Remote IRR is stuck set\n", ioapic->id, pin
        );
    }
}

static error_t INIT_CODE ioapic_domains_init(void)
{
    struct ioapic *ioapic;
    size_t i;
    u32 pin, num_pins;

    for (i = 0; i < s_num_ioapics; i++) {
        ioapic = &s_ioapics[i];

        // Nothing may be live before the first request
        num_pins = ioapic_num_pins(ioapic);
        for (pin = 0; pin < num_pins; pin++)
            ioapic_quiesce_pin(ioapic, pin);

        snprintf(ioapic->name, sizeof(ioapic->name), "ioapic-%zu", i);
        ioapic->domain = (struct irq_domain) {
            .name = ioapic->name,
            .ops = &s_ioapic_domain_ops,
            .priv = ioapic,
        };
        irq_domain_register(&ioapic->domain, &g_x86_lapic_domain);
    }

    return EOK;
}
INIT_CALL_PRE(IRQS_AVAILABLE, ioapic_domains_init);

// Nothing is behind the window if every register reads as all ones
static bool INIT_CODE ioapic_is_absent(struct ioapic *ioapic)
{
    u32 value;

    value = ioapic_read(ioapic, IOAPIC_REG_ID);
    value &= ioapic_read(ioapic, IOAPIC_REG_VER);
    value &= ioapic_read(ioapic, IOAPIC_REG_ARB);

    return value == 0xFFFFFFFF;
}

void INIT_CODE ioapic_register(u8 id, phys_addr_t base, u32 gsi_base)
{
    struct ioapic *new_ioapic;
    error_t ret;
    const char *why;
    u32 actual_id, version;

    // GSI resolution relies on every IOAPIC being known by then
    BUG_ON_INIT_LEVEL_AT_OR_ABOVE(IRQS_AVAILABLE);

    new_ioapic = ioapic_next_empty_slot();
    if (unlikely(!new_ioapic)) {
        why = "no space left";
        goto out_no_reg;
    }

    if (unlikely(base == 0)) {
        why = "invalid base address";
        goto out_no_reg;
    }

    ret = io_window_map(&new_ioapic->iow, base, PAGE_SIZE);
    if (is_error(ret)) {
        why = "unable to map";
        goto out_no_reg;
    }

    if (unlikely(ioapic_is_absent(new_ioapic))) {
        why = "not present";
        goto out_unmap;
    }

    actual_id = BIT_FIELD_READ(
        ioapic_read(new_ioapic, IOAPIC_REG_ID),
        IOAPIC_ID
    );

    if (actual_id != id) {
        pr_debug(
            "hardware id %u differs from the ACPI-provided %u\n",
            actual_id, id
        );
    }

    new_ioapic->id = id;
    new_ioapic->base = base;
    new_ioapic->gsi_base = gsi_base;

    version = ioapic_read(new_ioapic, IOAPIC_REG_VER);
    new_ioapic->version = BIT_FIELD_READ(version, IOAPIC_VERSION);
    new_ioapic->gsi_last =
        gsi_base + BIT_FIELD_READ(version, IOAPIC_MAX_REDIR_ENTRY);

    if (unlikely(ioapic_check_collisions(new_ioapic))) {
        io_window_unmap(&new_ioapic->iow);
        return;
    }

    spin_lock_init(&new_ioapic->lock);
    s_num_ioapics++;
    pr_info(
        "registered IOAPIC[%u] (0x%llX, GSI[%u->%u])\n",
        new_ioapic->id, new_ioapic->base,
        new_ioapic->gsi_base, new_ioapic->gsi_last
    );
    return;

out_unmap:
    io_window_unmap(&new_ioapic->iow);
out_no_reg:
    pr_warn(
        "unable to register IOAPIC[%u] (0x%llX, GSI base %u): %s\n",
        id, base, gsi_base, why
    );
}
