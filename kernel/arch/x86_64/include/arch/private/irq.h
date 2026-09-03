#pragma once

#include <common/bit.h>

struct irq_domain;

/*
 * The x86 layout of the MSI address word. Hypervisors that support
 * extended destination ids carry bits 8-14 in the reserved bits
 * below the legacy byte, so the id is split across two fields.
 */
#define X86_MSI_ADDR_BASE 0xFEE00000
#define X86_MSI_ADDR_DESTID_0_7_MASK MAKE_BIT_MASK_U32(19, 12)
#define X86_MSI_ADDR_VIRT_DESTID_8_14_MASK MAKE_BIT_MASK_U32(11, 5)

// The root of the interrupt domain hierarchy on x86
extern struct irq_domain g_x86_lapic_domain;
