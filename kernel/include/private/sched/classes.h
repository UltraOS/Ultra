#pragma once

#include <common/helpers.h>

#define SCHED_CLASSES_SECTION sched_classes
#define SCHED_CLASS_SECTION(name) CONCAT(sched_class_, name)

#define SCHED_CLASS_INSTANCE(name) g_##name##_sched_class
#define DECLARE_SCHED_CLASS(name)                              \
    extern const struct sched_class SCHED_CLASS_INSTANCE(name)

/*
 * All scheduling classes ordered by rank, higher first.
 *
 * NOTE: This ordering is further enforced by the linker, a higher rank class is
 *       always placed at a lower address. We use this fact for O(1) comparisons
 *       between the classes of different tasks.
 */
#define SCHED_CLASSES \
    SCHED_CLASS(rr)   \
    SCHED_CLASS(idle)
