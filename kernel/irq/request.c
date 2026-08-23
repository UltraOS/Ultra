#define MSG_FMT(msg) "irq: " msg

#include <private/irq.h>

#include <bug.h>
#include <mutex.h>
#include <init_level.h>
#include <memory/alloc.h>
#include <common/atomic.h>

#include <arch/cpu_helpers.h>
#include <irq_helpers.h>

static DEFINE_MUTEX(s_request_mutex);
static LIST_HEAD(s_requested_irqs);

static struct irq *irq_find(const struct irq_spec *spec)
{
    struct irq *irq;

    list_for_each_entry(irq, &s_requested_irqs, node) {
        if (irq->spec.domain != spec->domain)
            continue;
        if (irq->spec.line != spec->line)
            continue;

        return irq;
    }

    return NULL;
}

static bool trigger_is_valid(enum irq_trigger trigger)
{
    u32 mode, polarity;

    mode = trigger & IRQ_TRIGGER_MODE_MASK;
    polarity = trigger & IRQ_TRIGGER_POLARITY_MASK;

    if (trigger & ~(IRQ_TRIGGER_MODE_MASK | IRQ_TRIGGER_POLARITY_MASK))
        return false;
    if (mode != IRQ_TRIGGER_EDGE && mode != IRQ_TRIGGER_LEVEL)
        return false;
    if (polarity == 0)
        return false;

    // Only edge detection may fire on both polarities
    if (mode == IRQ_TRIGGER_LEVEL && polarity == IRQ_TRIGGER_POLARITY_MASK)
        return false;

    return true;
}

static error_t validate_request(
    const struct irq_spec *spec, enum irq_flags flags, void *user
)
{
    if (!trigger_is_valid(spec->trigger))
        return EINVAL;

    if (flags & IRQ_FLAG_SHARED) {
        if (!irq_trigger_is_level(spec->trigger))
            return EINVAL;
        if (user == NULL)
            return EINVAL;

        /*
         * The enable state of a shared line belongs to every handler
         * on it, no single requester may demand it starts disabled.
         */
        if (flags & IRQ_FLAG_START_DISABLED)
            return EINVAL;
    }

    return EOK;
}

/*
 * Leaf first, the reverse of allocation. A failed walk unwinds from
 * the level above the one that failed.
 */
static void irq_levels_free(struct irq *irq, u32 first)
{
    struct irq_level *level;
    u32 i;

    for (i = first; i < irq->num_levels; i++) {
        level = &irq->levels[i];

        if (level->domain->ops->free)
            level->domain->ops->free(irq, level);
    }
}

// Parents allocate first so lower levels can build on their route
static error_t irq_levels_alloc(
    struct irq *irq, struct irq_alloc_request *desc
)
{
    struct irq_level *level;
    error_t ret;
    u32 i = irq->num_levels;

    while (i--) {
        level = &irq->levels[i];

        ret = level->domain->ops->alloc(irq, level, desc);
        if (is_error(ret)) {
            irq_levels_free(irq, i + 1);
            return ret;
        }

        BUG_ON(level->chip == NULL);
    }

    return EOK;
}

static void irq_levels_deactivate(struct irq *irq, u32 first)
{
    struct irq_level *level;
    u32 i;

    for (i = first; i < irq->num_levels; i++) {
        level = &irq->levels[i];

        if (level->domain->ops->deactivate)
            level->domain->ops->deactivate(irq, level);
    }
}

// Same order as allocation, routes are programmed CPU-first
static error_t irq_levels_activate(struct irq *irq)
{
    struct irq_level *level;
    error_t ret;
    u32 i = irq->num_levels;

    while (i--) {
        level = &irq->levels[i];

        if (level->domain->ops->activate == NULL)
            continue;

        ret = level->domain->ops->activate(irq, level);
        if (is_error(ret)) {
            irq_levels_deactivate(irq, i + 1);
            return ret;
        }
    }

    return EOK;
}

static error_t irq_object_create(
    struct irq_alloc_request *desc, struct irq **out
)
{
    struct irq *irq;
    struct irq_domain *domain;
    error_t ret;
    u32 i, depth = 0;

    for (domain = desc->spec.domain; domain; domain = domain->parent)
        depth++;

    BUG_ON(depth == 0 || depth > MAX_NESTED_IRQ_DOMAINS);

    irq = alloc(sizeof(*irq), ALLOC_GENERIC_ZEROED);
    if (irq == NULL)
        return ENOMEM;

    irq->spec = desc->spec;
    irq->num_levels = depth;
    spin_lock_init(&irq->lock);
    list_init(&irq->actions);
    list_init(&irq->node);

    if (irq_trigger_is_level(desc->spec.trigger))
        irq->flow = irq_handle_level;
    else
        irq->flow = irq_handle_edge;

    domain = desc->spec.domain;
    for (i = 0; i < depth; i++) {
        irq->levels[i].domain = domain;
        domain = domain->parent;
    }

    ret = irq_levels_alloc(irq, desc);
    if (is_error(ret)) {
        free(irq);
        return ret;
    }

    *out = irq;
    return EOK;
}

/*
 * Acquire the lock with no delivery walk in flight, the only state
 * in which the action list may be mutated: the walk runs with the
 * lock dropped and relies on the list staying untouched. The
 * lockless poll is only a hint that avoids hammering the lock the
 * delivery path needs to finish, the answer is trusted only when it
 * is reproduced under the lock, whose acquisition is also what
 * orders us against the completed walk.
 */
static irq_state_t irq_lock_quiesced(struct irq *irq)
{
    irq_state_t irq_state;

    for (;;) {
        while (atomic_load_relaxed(&irq->in_progress))
            arch_cpu_relax();

        irq_state = spin_lock_irq_save(&irq->lock);
        if (!atomic_load_relaxed(&irq->in_progress))
            return irq_state;

        spin_unlock_irq_restore(&irq->lock, irq_state);
    }
}

static error_t irq_action_add(
    struct irq *irq, irq_handler_t handler, void *user, const char *name
)
{
    struct irq_action *action;
    irq_state_t irq_state;

    action = alloc(sizeof(*action), ALLOC_GENERIC_ZEROED);
    if (action == NULL)
        return ENOMEM;

    action->handler = handler;
    action->user = user;
    action->name = name;

    irq_state = irq_lock_quiesced(irq);
    list_insert_prev(&irq->actions, &action->node);
    irq->num_actions++;
    spin_unlock_irq_restore(&irq->lock, irq_state);

    return EOK;
}

error_t irq_request_with_affinity(
    const struct irq_spec *spec, const struct cpu_mask *affinity,
    irq_handler_t handler, void *user, enum irq_flags flags,
    const char *name, struct irq **out_irq
)
{
    struct irq_alloc_request desc = { };
    struct irq *irq;
    error_t ret;

    BUG_ON_INIT_LEVEL_BELOW(IRQS_AVAILABLE);
    BUG_ON(handler == NULL);

    ret = validate_request(spec, flags, user);
    if (is_error(ret))
        return ret;

    mutex_lock(&s_request_mutex);

    irq = irq_find(spec);
    if (irq != NULL) {
        ret = EBUSY;
        if (!(flags & IRQ_FLAG_SHARED) || !(irq->flags & IRQ_FLAG_SHARED))
            goto out;

        // A sharer may not change line-wide properties
        ret = EINVAL;
        if (irq->spec.trigger != spec->trigger || affinity != NULL)
            goto out;

        ret = irq_action_add(irq, handler, user, name);
        goto out;
    }

    desc.spec = *spec;
    desc.affinity = affinity;

    ret = irq_object_create(&desc, &irq);
    if (is_error(ret))
        goto out;

    ret = irq_levels_activate(irq);
    if (is_error(ret))
        goto out_free_levels;

    ret = irq_action_add(irq, handler, user, name);
    if (is_error(ret))
        goto out_deactivate;

    irq->flags = flags & IRQ_FLAG_SHARED;

    // Activation leaves the line masked, delivery starts here
    if (flags & IRQ_FLAG_START_DISABLED) {
        irq->disable_depth = 1;
        irq->state = IRQ_STATE_MASKED;
    } else {
        irq_hw_unmask(irq);
    }

    list_insert_prev(&s_requested_irqs, &irq->node);

out:
    if (ret == EOK)
        *out_irq = irq;

    mutex_unlock(&s_request_mutex);
    return ret;

out_deactivate:
    irq_levels_deactivate(irq, 0);
out_free_levels:
    irq_levels_free(irq, 0);
    free(irq);

    mutex_unlock(&s_request_mutex);
    return ret;
}

void irq_free(struct irq *irq, void *user)
{
    struct irq_action *action, *found = NULL;
    irq_state_t irq_state;

    mutex_lock(&s_request_mutex);

    /*
     * Unlinking with no walk in flight is also the handler drain: an
     * invocation possibly running on another CPU has been waited
     * out, and no new walk can pick the action up again.
     */
    irq_state = irq_lock_quiesced(irq);

    list_for_each_entry(action, &irq->actions, node) {
        if (action->user != user)
            continue;

        found = action;
        break;
    }

    BUG_ON(found == NULL);

    list_remove(&found->node);
    irq->num_actions--;

    if (irq->num_actions == 0) {
        irq->state |= IRQ_STATE_MASKED;
        irq_hw_mask(irq);
    }

    spin_unlock_irq_restore(&irq->lock, irq_state);

    free(found);

    if (irq->num_actions == 0) {
        /*
         * Make sure to drain the chip if it records a pending and not yet
         * serviced IRQ in hardware. If we don't, we will get an unexpected
         * IRQ later, which will likely permanently jam the corresponding pin
         * since there's nothing to service the IRQ at that point.
         */
        irq_hw_drain(irq);
        irq_levels_deactivate(irq, 0);
        irq_levels_free(irq, 0);
        list_remove(&irq->node);
        free(irq);
    }

    mutex_unlock(&s_request_mutex);
}

void irq_enable(struct irq *irq)
{
    irq_state_t irq_state;

    irq_state = spin_lock_irq_save(&irq->lock);

    if (WARN_ON(!irq_disabled(irq)))
        goto out;
    if (--irq->disable_depth)
        goto out;

    if (irq->state & IRQ_STATE_MASKED) {
        irq->state &= ~IRQ_STATE_MASKED;
        irq_hw_unmask(irq);
    }

    if (irq->state & IRQ_STATE_PENDING) {
        irq->state &= ~IRQ_STATE_PENDING;
        WARN_ON(is_error(irq_hw_retrigger(irq)));
    }

out:
    spin_unlock_irq_restore(&irq->lock, irq_state);
}

void irq_synchronize_hard(struct irq *irq)
{
    irq_state_t irq_state;

    // A walk cannot wait for itself
    BUG_ON(in_hard_irq());

    irq_state = irq_lock_quiesced(irq);
    spin_unlock_irq_restore(&irq->lock, irq_state);
}

// Handlers only ever run in interrupt context, so the hard wait is all
void irq_synchronize(struct irq *irq)
{
    irq_synchronize_hard(irq);
}

void irq_disable_nosync(struct irq *irq)
{
    irq_state_t irq_state;

    irq_state = spin_lock_irq_save(&irq->lock);
    irq->disable_depth++;
    spin_unlock_irq_restore(&irq->lock, irq_state);
}

void irq_disable(struct irq *irq)
{
    irq_disable_nosync(irq);
    irq_synchronize(irq);
}
