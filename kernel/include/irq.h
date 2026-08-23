#pragma once

#include <config.h>

#include <common/types.h>
#include <common/bit.h>

/*
 * The maximum depth of the interrupt domain hierarchy, as in the
 * number of interrupt controllers an interrupt travels through on its
 * way to a CPU.
 */
#define MAX_NESTED_IRQ_DOMAINS CONFIG_MAX_NESTED_IRQ_DOMAINS

struct irq;

/*
 * A domain-specific IRQ line identifier, e.g. a controller pin or an MSI
 * table index. This is not intended to be used as a composite key, if your
 * domain requires it, consider splitting it into multiple sub-domains.
 */
typedef u32 irq_line_t;

/*
 * Interrupt triggering is described along two orthogonal axes, the
 * detection mode (edge or level) and the activation polarity. A well
 * formed trigger selects exactly one detection mode and at least one
 * polarity, only edge detection may select both polarities to fire
 * on either transition. Zero is reserved so that an unset trigger
 * stays detectable.
 */
enum irq_trigger : u32 {
    IRQ_TRIGGER_EDGE = BIT_U32(0),
    IRQ_TRIGGER_LEVEL = BIT_U32(1),
    IRQ_TRIGGER_ACTIVE_HIGH = BIT_U32(2),
    IRQ_TRIGGER_ACTIVE_LOW = BIT_U32(3),

    IRQ_TRIGGER_EDGE_ACTIVE_HIGH =
        IRQ_TRIGGER_EDGE | IRQ_TRIGGER_ACTIVE_HIGH,
    IRQ_TRIGGER_EDGE_ACTIVE_LOW =
        IRQ_TRIGGER_EDGE | IRQ_TRIGGER_ACTIVE_LOW,
    IRQ_TRIGGER_EDGE_BOTH =
        IRQ_TRIGGER_EDGE | IRQ_TRIGGER_ACTIVE_HIGH |
        IRQ_TRIGGER_ACTIVE_LOW,
    IRQ_TRIGGER_LEVEL_ACTIVE_HIGH =
        IRQ_TRIGGER_LEVEL | IRQ_TRIGGER_ACTIVE_HIGH,
    IRQ_TRIGGER_LEVEL_ACTIVE_LOW =
        IRQ_TRIGGER_LEVEL | IRQ_TRIGGER_ACTIVE_LOW,
};

#define IRQ_TRIGGER_MODE_MASK (IRQ_TRIGGER_EDGE | IRQ_TRIGGER_LEVEL)
#define IRQ_TRIGGER_POLARITY_MASK \
    (IRQ_TRIGGER_ACTIVE_HIGH | IRQ_TRIGGER_ACTIVE_LOW)
