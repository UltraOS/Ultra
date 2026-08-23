#include <stdlib.h>
#include <string.h>

#include <memory/alloc_behavior.h>

/*
 * The kernel heap references are rerouted to malloc so no real
 * allocator bring-up is needed. The rename stays active for the whole
 * file since it also renames the irq_domain_ops members of the same
 * name, so the implementations must be defined up here where the libc
 * free is still reachable.
 */
void *irq_test_alloc(size_t size, enum alloc_behavior behavior)
{
    void *ptr;

    ptr = malloc(size);
    if (ptr != NULL && (behavior & ALLOC_ZEROED))
        memset(ptr, 0, size);

    return ptr;
}

void irq_test_free(void *ptr)
{
    free(ptr);
}

#define alloc irq_test_alloc
#define free irq_test_free

#include <kernel-source/irq/chip.c>
#include <kernel-source/irq/domain.c>
#include <kernel-source/irq/request.c>
#include <kernel-source/irq/flow.c>

#include <cpu_mask.h>
#include <test_harness.h>

static const char *s_op_log[32];
static u32 s_num_ops;

static void log_op(const char *op)
{
    ASSERT(s_num_ops < 32);
    s_op_log[s_num_ops++] = op;
}

static void assert_ops(const char **expected, u32 count)
{
    u32 i;

    ASSERT_EQ(s_num_ops, count);
    for (i = 0; i < count; i++)
        ASSERT_STR_EQ(s_op_log[i], expected[i]);
}

#define ASSERT_OPS(...)                                             \
    do {                                                            \
        const char *expected[] = { __VA_ARGS__ };                   \
        assert_ops(expected, sizeof(expected) / sizeof(*expected)); \
    } while (0)

static struct irq_domain s_parent_domain;
static struct irq_domain s_leaf_domain;

static bool s_fail_leaf_alloc;
static bool s_fail_leaf_activate;
static bool s_leaf_latches;

static void fake_mask(struct irq_level *level)
{
    UNREFERENCED_PARAMETER(level);
    log_op("mask");
}

static void fake_unmask(struct irq_level *level)
{
    UNREFERENCED_PARAMETER(level);
    log_op("unmask");
}

static error_t fake_retrigger(struct irq_level *level)
{
    UNREFERENCED_PARAMETER(level);
    log_op("retrigger");
    return EOK;
}

static void fake_ack(struct irq_level *level)
{
    UNREFERENCED_PARAMETER(level);
    log_op("ack");
}

static void fake_eoi(struct irq_level *level)
{
    UNREFERENCED_PARAMETER(level);
    log_op("eoi");
}

static void fake_leaf_ack(struct irq_level *level)
{
    UNREFERENCED_PARAMETER(level);
    log_op("leaf-ack");
}

static void fake_leaf_eoi(struct irq_level *level)
{
    UNREFERENCED_PARAMETER(level);
    log_op("leaf-eoi");
}

// Reports outstanding this many more times, silent once drained
static u32 s_num_outstanding_polls;

static bool fake_is_outstanding(struct irq_level *level)
{
    UNREFERENCED_PARAMETER(level);

    if (s_num_outstanding_polls == 0)
        return false;

    s_num_outstanding_polls--;
    log_op("outstanding");
    return true;
}

/*
 * The leaf chip deliberately lacks ack/eoi/retrigger so the walks
 * are forced to delegate them to the parent level.
 */
static const struct irq_chip s_leaf_chip = {
    .name = "fake-leaf",
    .mask = fake_mask,
    .unmask = fake_unmask,
    .is_outstanding = fake_is_outstanding,
};

// A leaf with a latch of its own, on top of the same parent
static const struct irq_chip s_latching_leaf_chip = {
    .name = "fake-latching-leaf",
    .mask = fake_mask,
    .unmask = fake_unmask,
    .ack = fake_leaf_ack,
    .eoi = fake_leaf_eoi,
};

static const struct irq_chip s_parent_chip = {
    .name = "fake-parent",
    .ack = fake_ack,
    .eoi = fake_eoi,
    .retrigger = fake_retrigger,
};

static error_t fake_alloc(
    struct irq *irq, struct irq_level *level, struct irq_alloc_request *desc
)
{
    UNREFERENCED_PARAMETER(irq);

    if (level->domain == &s_leaf_domain) {
        log_op("leaf-alloc");

        if (s_fail_leaf_alloc)
            return EIO;

        level->chip = s_leaf_latches ? &s_latching_leaf_chip : &s_leaf_chip;
        level->line = desc->spec.line;
        return EOK;
    }

    log_op("parent-alloc");
    level->chip = &s_parent_chip;
    level->line = desc->spec.line + 100;
    return EOK;
}

static void fake_free(struct irq *irq, struct irq_level *level)
{
    UNREFERENCED_PARAMETER(irq);

    if (level->domain == &s_leaf_domain)
        log_op("leaf-free");
    else
        log_op("parent-free");
}

static error_t fake_activate(struct irq *irq, struct irq_level *level)
{
    UNREFERENCED_PARAMETER(irq);

    if (level->domain == &s_leaf_domain) {
        log_op("leaf-activate");

        if (s_fail_leaf_activate)
            return EIO;

        return EOK;
    }

    log_op("parent-activate");
    return EOK;
}

static void fake_deactivate(struct irq *irq, struct irq_level *level)
{
    UNREFERENCED_PARAMETER(irq);

    if (level->domain == &s_leaf_domain)
        log_op("leaf-deactivate");
    else
        log_op("parent-deactivate");
}

static const struct irq_domain_ops s_fake_ops = {
    .alloc = fake_alloc,
    .free = fake_free,
    .activate = fake_activate,
    .deactivate = fake_deactivate,
};

static struct irq_domain s_parent_domain = {
    .name = "fake-parent",
    .ops = &s_fake_ops,
};

static struct irq_domain s_leaf_domain = {
    .name = "fake-leaf",
    .ops = &s_fake_ops,
};

static void reset_state(void)
{
    s_num_ops = 0;
    s_fail_leaf_alloc = false;
    s_fail_leaf_activate = false;
    s_num_outstanding_polls = 0;
    s_leaf_latches = false;

    list_init(&s_requested_irqs);

    s_test_init_level = INIT_LEVEL_VALLOC_AVAILABLE;
    irq_domain_register(&s_parent_domain, NULL);
    irq_domain_register(&s_leaf_domain, &s_parent_domain);
    s_test_init_level = NUM_INIT_LEVELS;
}

static enum irq_result count_handler(void *user)
{
    u32 *counter = user;

    (*counter)++;
    return IRQ_RESULT_HANDLED;
}

static enum irq_result none_handler(void *user)
{
    u32 *counter = user;

    (*counter)++;
    return IRQ_RESULT_UNHANDLED;
}

static struct irq_spec make_spec(irq_line_t line, enum irq_trigger trigger)
{
    struct irq_spec spec = { };

    spec.domain = &s_leaf_domain;
    spec.line = line;
    spec.trigger = trigger;

    return spec;
}

TEST_CASE(irq_request_walks_the_hierarchy)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(7, IRQ_TRIGGER_EDGE_ACTIVE_HIGH);

    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );
    ASSERT_OPS(
        "parent-alloc", "leaf-alloc", "parent-activate", "leaf-activate",
        "unmask"
    );

    ASSERT_EQ(irq->num_levels, 2);
    ASSERT(irq->levels[0].domain == &s_leaf_domain);
    ASSERT(irq->levels[1].domain == &s_parent_domain);
    ASSERT(irq_level_parent(&irq->levels[0]) == &irq->levels[1]);
    ASSERT_EQ(irq->levels[0].line, 7);
    ASSERT_EQ(irq->levels[1].line, 107);
    ASSERT_EQ(irq->num_actions, 1);
    ASSERT_EQ(irq->state, 0);

    s_num_ops = 0;
    irq_free(irq, &counter);
    ASSERT_OPS(
        "mask", "leaf-deactivate", "parent-deactivate", "leaf-free",
        "parent-free"
    );
}

TEST_CASE(irq_request_shared_line)
{
    struct irq_spec spec;
    struct irq *irq, *other;
    struct cpu_mask mask = { };
    u32 c1 = 0, c2 = 0, c3 = 0;

    reset_state();
    spec = make_spec(4, IRQ_TRIGGER_LEVEL_ACTIVE_LOW);

    ASSERT_EQ(
        irq_request(&spec, count_handler, &c1, IRQ_FLAG_SHARED, "a", &irq),
        EOK
    );
    ASSERT_EQ(
        irq_request(&spec, count_handler, &c2, IRQ_FLAG_SHARED, "b", &other),
        EOK
    );
    ASSERT(other == irq);
    ASSERT_EQ(irq->num_actions, 2);

    // An exclusive request must not join an existing line
    ASSERT_EQ(
        irq_request(&spec, count_handler, &c3, IRQ_FLAG_NONE, "c", &other),
        EBUSY
    );

    // Neither may one whose trigger disagrees with the line
    spec.trigger = IRQ_TRIGGER_LEVEL_ACTIVE_HIGH;
    ASSERT_EQ(
        irq_request(&spec, count_handler, &c3, IRQ_FLAG_SHARED, "c", &other),
        EINVAL
    );

    // Nor one that asks for its own placement
    spec.trigger = IRQ_TRIGGER_LEVEL_ACTIVE_LOW;
    ASSERT_EQ(
        irq_request_with_affinity(&spec, &mask, count_handler, &c3,
                                  IRQ_FLAG_SHARED, "c", &other),
        EINVAL
    );

    s_num_ops = 0;
    irq_free(irq, &c1);
    ASSERT_EQ(s_num_ops, 0);
    ASSERT_EQ(irq->num_actions, 1);

    irq_free(irq, &c2);
    ASSERT_OPS(
        "mask", "leaf-deactivate", "parent-deactivate", "leaf-free",
        "parent-free"
    );
}

TEST_CASE(irq_request_validation)
{
    static const enum irq_trigger bad_triggers[] = {
        0,
        IRQ_TRIGGER_EDGE,
        IRQ_TRIGGER_ACTIVE_LOW,
        IRQ_TRIGGER_EDGE | IRQ_TRIGGER_LEVEL | IRQ_TRIGGER_ACTIVE_HIGH,
        IRQ_TRIGGER_LEVEL | IRQ_TRIGGER_ACTIVE_HIGH |
            IRQ_TRIGGER_ACTIVE_LOW,
        IRQ_TRIGGER_EDGE_ACTIVE_HIGH | BIT_U32(4),
    };
    struct irq_spec spec;
    struct irq *irq;
    u32 i, counter = 0;

    reset_state();

    for (i = 0; i < ARRAY_SIZE(bad_triggers); i++) {
        spec = make_spec(1, bad_triggers[i]);
        ASSERT_EQ(
            irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE,
                        "t", &irq),
            EINVAL
        );
    }

    spec = make_spec(1, IRQ_TRIGGER_EDGE_ACTIVE_HIGH);
    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_SHARED, "t",
                    &irq),
        EINVAL
    );

    spec = make_spec(1, IRQ_TRIGGER_LEVEL_ACTIVE_HIGH);
    ASSERT_EQ(
        irq_request(&spec, count_handler, NULL, IRQ_FLAG_SHARED, "t", &irq),
        EINVAL
    );

    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter,
                    IRQ_FLAG_SHARED | IRQ_FLAG_START_DISABLED, "t", &irq),
        EINVAL
    );

    // Validation must reject before any domain gets involved
    ASSERT_EQ(s_num_ops, 0);
}

TEST_CASE(irq_start_disabled_enable_disable)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(2, IRQ_TRIGGER_EDGE_ACTIVE_HIGH);

    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter,
                    IRQ_FLAG_START_DISABLED, "t", &irq),
        EOK
    );
    ASSERT_OPS(
        "parent-alloc", "leaf-alloc", "parent-activate", "leaf-activate"
    );
    ASSERT_EQ(irq->state, IRQ_STATE_MASKED);
    ASSERT_EQ(irq->disable_depth, 1);

    s_num_ops = 0;
    irq_enable(irq);
    ASSERT_OPS("unmask");
    ASSERT_EQ(irq->state, 0);
    ASSERT_EQ(irq->disable_depth, 0);

    // Disabling is lazy, no hardware operation may be performed
    s_num_ops = 0;
    irq_disable(irq);
    ASSERT_EQ(s_num_ops, 0);
    ASSERT_EQ(irq->disable_depth, 1);

    // Disables from independent callers nest
    irq_disable_nosync(irq);
    ASSERT_EQ(irq->disable_depth, 2);

    irq_enable(irq);
    ASSERT_EQ(s_num_ops, 0);
    ASSERT_EQ(irq->disable_depth, 1);

    /*
     * Pretend the flow masked the line and latched an occurrence
     * while it was disabled: the last enable must unmask and then
     * replay via the parent chip, the only level that implements
     * retrigger.
     */
    irq->state |= IRQ_STATE_MASKED | IRQ_STATE_PENDING;
    irq_enable(irq);
    ASSERT_OPS("unmask", "retrigger");
    ASSERT_EQ(irq->state, 0);
    ASSERT_EQ(irq->disable_depth, 0);

    s_num_ops = 0;
    irq_free(irq, &counter);
}

static struct irq *s_sync_irq;
static u32 s_sync_num_relaxes;

// Stands in for the other CPU finishing its walk
static void sync_finish_walk(void)
{
    if (++s_sync_num_relaxes == 3)
        atomic_store_relaxed(&s_sync_irq->in_progress, false);
}

TEST_CASE(irq_synchronize_waits_out_walk)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(3, IRQ_TRIGGER_EDGE_ACTIVE_HIGH);

    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );

    // Nothing in flight, the wait returns at once
    g_cpu_relax_hook = sync_finish_walk;
    s_sync_irq = irq;
    s_sync_num_relaxes = 0;
    irq_synchronize(irq);
    ASSERT_EQ(s_sync_num_relaxes, 0);

    atomic_store_relaxed(&irq->in_progress, true);
    irq_synchronize(irq);
    ASSERT_EQ(s_sync_num_relaxes, 3);
    ASSERT_FALSE(irq->in_progress);

    s_sync_num_relaxes = 0;
    atomic_store_relaxed(&irq->in_progress, true);
    irq_synchronize_hard(irq);
    ASSERT_EQ(s_sync_num_relaxes, 3);
    ASSERT_FALSE(irq->in_progress);

    // The plain disable is the waiting one
    s_sync_num_relaxes = 0;
    atomic_store_relaxed(&irq->in_progress, true);
    irq_disable(irq);
    ASSERT_EQ(s_sync_num_relaxes, 3);
    ASSERT_EQ(irq->disable_depth, 1);

    s_sync_num_relaxes = 0;
    atomic_store_relaxed(&irq->in_progress, true);
    irq_disable_nosync(irq);
    ASSERT_EQ(s_sync_num_relaxes, 0);
    ASSERT_EQ(irq->disable_depth, 2);
    atomic_store_relaxed(&irq->in_progress, false);

    g_cpu_relax_hook = NULL;
    irq_enable(irq);
    irq_enable(irq);
    irq_free(irq, &counter);
}

static u32 s_num_relaxes;

static void count_relax(void)
{
    s_num_relaxes++;
}

TEST_CASE(irq_free_drains_outstanding)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(4, IRQ_TRIGGER_LEVEL_ACTIVE_LOW);

    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );

    // The route stays intact until the chip reports the line drained
    s_num_ops = 0;
    s_num_outstanding_polls = 2;
    s_num_relaxes = 0;
    g_cpu_relax_hook = count_relax;
    irq_free(irq, &counter);
    g_cpu_relax_hook = NULL;

    ASSERT_OPS(
        "mask", "outstanding", "outstanding", "leaf-deactivate",
        "parent-deactivate", "leaf-free", "parent-free"
    );
    ASSERT_EQ(s_num_relaxes, 2);
}

TEST_CASE(irq_alloc_failure_unwinds)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(3, IRQ_TRIGGER_EDGE_ACTIVE_HIGH);

    s_fail_leaf_alloc = true;
    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EIO
    );
    ASSERT_OPS("parent-alloc", "leaf-alloc", "parent-free");

    // The failed line must not have been left behind in the registry
    s_fail_leaf_alloc = false;
    s_num_ops = 0;
    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );
    ASSERT_OPS(
        "parent-alloc", "leaf-alloc", "parent-activate", "leaf-activate",
        "unmask"
    );

    irq_free(irq, &counter);
}

TEST_CASE(irq_activate_failure_unwinds)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(5, IRQ_TRIGGER_EDGE_ACTIVE_HIGH);

    s_fail_leaf_activate = true;
    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EIO
    );
    ASSERT_OPS(
        "parent-alloc", "leaf-alloc", "parent-activate", "leaf-activate",
        "parent-deactivate", "leaf-free", "parent-free"
    );
}

TEST_CASE(irq_flow_edge_delivery)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(8, IRQ_TRIGGER_EDGE_ACTIVE_HIGH);

    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );
    ASSERT(irq->flow == irq_handle_edge);

    // The latch is released before the handlers run
    s_num_ops = 0;
    irq_deliver(irq);
    ASSERT_OPS("ack");
    ASSERT_EQ(counter, 1);
    ASSERT_EQ(irq->state, 0);
    ASSERT_FALSE(irq->in_progress);

    ASSERT_EQ(irq->num_unhandled, 0);

    irq_free(irq, &counter);
}

TEST_CASE(irq_flow_edge_lazy_disable)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(9, IRQ_TRIGGER_EDGE_ACTIVE_HIGH);

    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );

    irq_disable(irq);

    /*
     * Delivery while disabled must not run the handlers: the
     * occurrence moves into the software latch and the line is
     * masked so it stays quiet.
     */
    s_num_ops = 0;
    irq_deliver(irq);
    ASSERT_OPS("mask", "ack");
    ASSERT_EQ(counter, 0);
    ASSERT_EQ(irq->state, IRQ_STATE_MASKED | IRQ_STATE_PENDING);

    s_num_ops = 0;
    irq_enable(irq);
    ASSERT_OPS("unmask", "retrigger");
    ASSERT_EQ(irq->state, 0);

    // The retrigger's re-delivery is an ordinary occurrence
    irq_deliver(irq);
    ASSERT_EQ(counter, 1);

    irq_free(irq, &counter);
}

TEST_CASE(irq_flow_level_delivery)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(10, IRQ_TRIGGER_LEVEL_ACTIVE_HIGH);

    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );
    ASSERT(irq->flow == irq_handle_level);

    // No masking on the happy path, EOI after the handlers
    s_num_ops = 0;
    irq_deliver(irq);
    ASSERT_OPS("eoi");
    ASSERT_EQ(counter, 1);
    ASSERT_EQ(irq->state, 0);
    ASSERT_EQ(irq->num_unhandled, 0);

    irq_free(irq, &counter);
}

TEST_CASE(irq_flow_ack_eoi_reach_every_level)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    s_leaf_latches = true;

    // Leaf latch first, then the parent releases
    spec = make_spec(14, IRQ_TRIGGER_EDGE_ACTIVE_HIGH);
    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );
    s_num_ops = 0;
    irq_deliver(irq);
    ASSERT_OPS("leaf-ack", "ack");
    ASSERT_EQ(counter, 1);

    // Masking stays with the closest implementer, ack still walks
    irq_disable(irq);
    s_num_ops = 0;
    irq_deliver(irq);
    ASSERT_OPS("mask", "leaf-ack", "ack");
    ASSERT_EQ(counter, 1);
    irq_enable(irq);
    irq_free(irq, &counter);

    spec = make_spec(15, IRQ_TRIGGER_LEVEL_ACTIVE_HIGH);
    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );
    s_num_ops = 0;
    irq_deliver(irq);
    ASSERT_OPS("leaf-eoi", "eoi");
    irq_free(irq, &counter);
}

TEST_CASE(irq_flow_level_lazy_disable)
{
    struct irq_spec spec;
    struct irq *irq;
    u32 counter = 0;

    reset_state();
    spec = make_spec(11, IRQ_TRIGGER_LEVEL_ACTIVE_HIGH);

    ASSERT_EQ(
        irq_request(&spec, count_handler, &counter, IRQ_FLAG_NONE, "t",
                    &irq),
        EOK
    );

    irq_disable(irq);

    /*
     * No software latch for level: the still-asserted line re-fires
     * by itself once unmasked, so enable must not retrigger either.
     */
    s_num_ops = 0;
    irq_deliver(irq);
    ASSERT_OPS("mask", "eoi");
    ASSERT_EQ(counter, 0);
    ASSERT_EQ(irq->state, IRQ_STATE_MASKED);

    s_num_ops = 0;
    irq_enable(irq);
    ASSERT_OPS("unmask");
    ASSERT_EQ(irq->state, 0);

    irq_deliver(irq);
    ASSERT_EQ(counter, 1);

    irq_free(irq, &counter);
}

TEST_CASE(irq_flow_shared_walk_accounting)
{
    struct irq_spec spec;
    struct irq *irq, *other;
    u32 c1 = 0, c2 = 0;

    reset_state();
    spec = make_spec(12, IRQ_TRIGGER_LEVEL_ACTIVE_LOW);

    ASSERT_EQ(
        irq_request(&spec, none_handler, &c1, IRQ_FLAG_SHARED, "a", &irq),
        EOK
    );
    ASSERT_EQ(
        irq_request(&spec, count_handler, &c2, IRQ_FLAG_SHARED, "b", &other),
        EOK
    );

    // Every sharer is invoked, one claiming the occurrence is enough
    irq_deliver(irq);
    ASSERT_EQ(c1, 1);
    ASSERT_EQ(c2, 1);
    ASSERT_EQ(irq->num_unhandled, 0);

    // With the claiming handler gone the occurrence goes unclaimed
    irq_free(irq, &c2);
    irq_deliver(irq);
    ASSERT_EQ(c1, 2);
    ASSERT_EQ(irq->num_unhandled, 1);

    irq_free(irq, &c1);
}
