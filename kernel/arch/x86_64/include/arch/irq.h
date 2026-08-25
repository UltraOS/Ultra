#pragma once

#include <irq.h>

/*
 * Translate a legacy ISA IRQ number into the interrupt spec serving
 * it. The ISA namespace is distinct from the GSI space: overrides
 * may move a line and change how it fires, e.g. IRQ 0 typically
 * arrives on GSI 2.
 */
error_t isa_irq_get(u8 isa_irq, struct irq_spec *out);
