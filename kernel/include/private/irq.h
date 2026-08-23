#pragma once

#include <irq.h>

#include <common/types.h>
#include <common/error.h>

struct irq_domain;
struct irq_chip;
struct irq_alloc_request;

/*
 * One entry per interrupt controller an interrupt travels through,
 * with the parent controller (toward the CPU) always adjacent.
 */
struct irq_level {
    struct irq_domain *domain;
    const struct irq_chip *chip;
    void *chip_data;
    irq_line_t line;
};

static inline struct irq_level *irq_level_parent(struct irq_level *level)
{
    return level + 1;
}

/*
 * The register access mechanism of one interrupt controller type,
 * shared by every line the controller type serves. Per-line state
 * lives in the level's chip_data.
 */
struct irq_chip {
    const char *name;

    /*
     * Gate delivery of the line.
     */
    void (*mask)(struct irq_level*);
    void (*unmask)(struct irq_level*);

    /*
     * Acknowledge the current occurrence before it is handled,
     * releasing the hardware latch so that a new occurrence arriving
     * mid-handling gets recorded instead of lost. Invoked by edge
     * flows prior to running the handlers.
     */
    void (*ack)(struct irq_level*);

    /*
     * Signal the end of servicing, releasing the controller's
     * in-service gating so the line may be delivered again. Invoked
     * by level flows after running the handlers.
     */
    void (*eoi)(struct irq_level*);

    /*
     * Make the interrupt fire again as if the hardware had delivered
     * a new occurrence. Used to replay occurrences that were latched
     * in software while delivery was not possible, e.g. an edge that
     * arrived while the line was disabled. Invoked under the
     * interrupt's lock with interrupts disabled.
     */
    error_t (*retrigger)(struct irq_level*);

    /*
     * Whether the chip still holds an occurrence of the line that no
     * CPU has serviced and acknowledged yet. Used when this IRQ is
     * being released to fully drain the chip and avoid unexpected
     * interrupts & permanently jammed pins.
     */
    bool (*is_outstanding)(struct irq_level*);
};

/*
 * The level argument is this domain's own entry in the interrupt.
 * Only alloc is mandatory.
 */
struct irq_domain_ops {
    // Pick a route for the line and fill in the entry
    error_t (*alloc)(
        struct irq*, struct irq_level*, struct irq_alloc_request*
    );
    void (*free)(struct irq*, struct irq_level*);

    // Program the picked route into the hardware, leaving the line masked
    error_t (*activate)(struct irq*, struct irq_level*);

    // Wipe the route, the core has masked and drained the line already
    void (*deactivate)(struct irq*, struct irq_level*);
};

struct irq_domain {
    const char *name;
    const struct irq_domain_ops *ops;
    struct irq_domain *parent;
    void *priv;
};

/*
 * Domains may be registered at any time. Whether one has to exist
 * before interrupts become requestable is the business of the
 * resolver that hands it out.
 */
void irq_domain_register(struct irq_domain*, struct irq_domain *parent);
