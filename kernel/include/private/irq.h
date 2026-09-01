#pragma once

#include <irq.h>

#include <common/types.h>
#include <common/error.h>
#include <common/list.h>

#include <spinlock.h>

struct irq_domain;
struct irq_chip;

/*
 * Allocation facts only the architecture's own levels fill and read.
 * An architecture with such facts defines the struct in
 * <arch/irq_alloc_request.h>, the empty default covers the rest.
 */
#if HAS_INCLUDE(<arch/irq_alloc_request.h>)
#include <arch/irq_alloc_request.h>
#else
struct arch_irq_alloc_request {
};
#endif

/*
 * Built by the core from the request arguments and handed to every
 * alloc callback on the walk. Lives only for the walk, nothing in it
 * may be retained.
 */
struct irq_alloc_request {
    struct irq_spec spec;

    // Optional set of CPUs the interrupt may be delivered to
    const struct cpu_mask *affinity;

    struct arch_irq_alloc_request arch;
};

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

enum irq_state : u32 {
    /*
     * The line is masked at the hardware and must be unmasked on
     * enable.
     */
    IRQ_STATE_MASKED = BIT_U32(0),

    /*
     * An occurrence arrived while the line was disabled and must be
     * replayed on enable.
     */
    IRQ_STATE_PENDING = BIT_U32(1),
};

struct irq_action {
    irq_handler_t handler;
    void *user;
    const char *name;
    struct list_link node;
};

struct irq {
    struct irq_spec spec;

    // Only the line-wide flags every requester must agree on
    enum irq_flags flags;

    // Protects state and the action list against the delivery path
    struct spinlock lock;
    enum irq_state state;

    /*
     * Set under the lock by a delivery walk, which then runs with the
     * lock dropped. Polled locklessly by the request path, so every
     * access is a relaxed atomic.
     */
    bool in_progress;

    // Disabled while non-zero
    u32 disable_depth;

    // Only mutated under the lock with no delivery walk in flight
    struct list_link actions;
    u32 num_actions;

    // Linkage in the global list of requested interrupts
    struct list_link node;

    u32 num_levels;
    struct irq_level levels[MAX_NESTED_IRQ_DOMAINS];
};

static inline bool irq_disabled(struct irq *irq)
{
    return irq->disable_depth != 0;
}

/*
 * Hardware operations act on the entire line: the level closest to
 * the requested one whose chip implements the operation performs it.
 */
void irq_hw_mask(struct irq*);
void irq_hw_unmask(struct irq*);
error_t irq_hw_retrigger(struct irq*);

/*
 * Whether any level still holds an occurrence of the line, and the
 * wait for that to end. The line must be masked so nothing new can
 * become outstanding, and interrupts must be on since the occurrence
 * may be this CPU's to take.
 */
bool irq_hw_is_outstanding(struct irq*);
void irq_hw_drain(struct irq*);
