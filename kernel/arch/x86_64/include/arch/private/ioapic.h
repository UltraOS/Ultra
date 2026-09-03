#pragma once

#include <common/types.h>
#include <common/error.h>
#include <irq.h>

#define NUM_ISA_IRQS 16

void ioapic_register(u8 id, phys_addr_t base, u32 gsi_base);

// Translate a GSI to the domain and pin serving it
error_t ioapic_gsi_to_pin(
    u32 gsi, struct irq_domain **out_domain, irq_line_t *out_pin
);

void ioapic_register_isa_irq_override(
    u8 irq, u32 gsi, u8 polarity, u8 triggering
);

void ioapic_finalize_overrides(void);
