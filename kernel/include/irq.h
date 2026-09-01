#pragma once

#include <config.h>

#include <common/types.h>
#include <common/error.h>
#include <common/bit.h>

/*
 * The maximum depth of the interrupt domain hierarchy, as in the
 * number of interrupt controllers an interrupt travels through on its
 * way to a CPU.
 */
#define MAX_NESTED_IRQ_DOMAINS CONFIG_MAX_NESTED_IRQ_DOMAINS

struct irq;
struct irq_domain;
struct cpu_mask;

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

static inline bool irq_trigger_is_level(enum irq_trigger trigger)
{
    return (trigger & IRQ_TRIGGER_LEVEL) != 0;
}

/*
 * The identity of an interrupt line: the controller it enters the
 * hierarchy through, the line number in that controller's own
 * namespace, and the way the hardware signals an occurrence. Produced
 * by resolvers, consumed by irq_request().
 */
struct irq_spec {
    struct irq_domain *domain;
    irq_line_t line;
    enum irq_trigger trigger;
};

enum irq_result {
    IRQ_RESULT_UNHANDLED,
    IRQ_RESULT_HANDLED,
};

typedef enum irq_result (*irq_handler_t)(void *user);

enum irq_flags : u32 {
    IRQ_FLAG_NONE = 0,

    /*
     * Allow other handlers on the same line. Only valid for level
     * triggered interrupts, every requester of the line must pass
     * it, and the user pointer must not be NULL, as it is what
     * identifies the handler among the sharers.
     */
    IRQ_FLAG_SHARED = BIT_U32(0),

    /*
     * Leave the line disabled until the caller invokes irq_enable().
     */
    IRQ_FLAG_START_DISABLED = BIT_U32(1),
};

/*
 * The affinity mask optionally restricts the set of CPUs the
 * interrupt may be delivered to, NULL means any online CPU.
 */
error_t irq_request_with_affinity(
    const struct irq_spec*, const struct cpu_mask *affinity,
    irq_handler_t handler, void *user, enum irq_flags flags,
    const char *name, struct irq **out_irq
);

static inline error_t irq_request(
    const struct irq_spec *spec, irq_handler_t handler, void *user,
    enum irq_flags flags, const char *name, struct irq **out_irq
)
{
    return irq_request_with_affinity(
        spec, NULL, handler, user, flags, name, out_irq
    );
}

/*
 * Remove the handler identified by the user pointer, waiting out any
 * currently running invocation of it. The line is torn down once the
 * last handler is removed. Must not be called from the handler.
 */
void irq_free(struct irq*, void *user);

/*
 * Wait until no delivery of the line is in flight, so that whatever
 * the handlers touch may be torn down. Must not be called from the
 * handler. The hard form waits only for the part that runs in
 * interrupt context and never sleeps.
 */
void irq_synchronize(struct irq*);
void irq_synchronize_hard(struct irq*);

void irq_enable(struct irq*);

/*
 * Disable an IRQ.
 * This uses a nested disable count internally so that multiple owners of a
 * shared line can disable/enable without trampling on each other.
 *
 * irq_disable() also acts as an IRQ barrier, that is, it is guaranteed that
 * there's no handler running once it returns. Use irq_disable_nosync() if
 * that guarantee is not needed for the particular disable.
 */
void irq_disable(struct irq*);
void irq_disable_nosync(struct irq*);
