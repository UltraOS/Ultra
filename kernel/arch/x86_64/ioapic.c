#define MSG_FMT(msg) "ioapic: " msg

#include <arch/private/ioapic.h>
#include <arch/constants.h>

#include <log.h>
#include <bug.h>
#include <free_after_init.h>

#include <memory/io.h>

#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#include <common/string.h>
#include <common/bit.h>

#define IOAPIC_IOREGSEL 0x00
#define IOAPIC_IOWIN 0x10

enum ioapic_reg {
    IOAPIC_REG_ID = 0x00,
        #define IOAPIC_ID MAKE_BIT_MASK(31, 24)

    IOAPIC_REG_VER = 0x01,
        #define IOAPIC_MAX_REDIR_ENTRY MAKE_BIT_MASK(23, 16)

    IOAPIC_REG_ARB = 0x02,
    IOAPIC_REG_IOREDTBL = 0x10,
};

struct ioapic {
    u8 id;
    phys_addr_t base;
    io_window iow;
    u32 gsi_base, gsi_last;
};

#define MAX_IOAPICS 128
#define IOAPIC_IDX_UNKNOWN 0xFF

static struct ioapic s_ioapics[MAX_IOAPICS];
static size_t s_num_ioapics;

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

    if (WARN_ON(irq > NUM_ISA_IRQS))
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
    u32 actual_id;

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
    new_ioapic->gsi_last = gsi_base + BIT_FIELD_READ(
        ioapic_read(new_ioapic, IOAPIC_REG_VER), IOAPIC_MAX_REDIR_ENTRY
    );

    if (unlikely(ioapic_check_collisions(new_ioapic))) {
        io_window_unmap(&new_ioapic->iow);
        return;
    }

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
