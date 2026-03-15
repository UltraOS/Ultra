#pragma once

#include <common/types.h>

#define NUM_ISA_IRQS 16

void ioapic_register(u8 id, phys_addr_t base, u32 gsi_base);

void ioapic_register_isa_irq_override(
    u8 irq, u32 gsi, u8 polarity, u8 triggering
);

void ioapic_finalize_overrides(void);
