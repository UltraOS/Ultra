#pragma once

#include <common/attributes.h>
#include <common/types.h>
#include <common/error.h>

#include <private/init_level.h>

typedef error_t (*init_call_t)(void);

#ifndef ULTRA_RUNTIME_MODULE

#define MAKE_INIT_CALL(func, level, type)                  \
    STATIC_ASSERT(                                         \
        INIT_LEVEL_##level >= 0,                           \
        "Please use one of init_level enumeration values " \
        "(without a prefix)"                               \
    );                                                     \
    SECTION_VAR(                                           \
        INIT_LEVEL_CB_SECTION(level, type),                \
        static const, init_call_t                          \
    )                                                      \
    init_call_hook_##func = func

/*
 * Every init level names a promise about kernel state (e.g. the heap
 * being usable). Three kinds of callbacks can be attached to a level:
 *
 * - INIT_CALL_AT: the unique callback that establishes the level's
 *   promise. At most one may exist per level, and a level without one
 *   is established by the raise itself. A failure here is fatal.
 * - INIT_CALL_PRE: runs after the AT callback, before the level is
 *   observable via init_level().
 * - INIT_CALL_POST: runs once the level is observable.
 *
 * Callbacks of the same kind are not ordered relative to each other
 * and must not depend on one another. A required sequence is either an
 * explicit call chain in code or a reason to split the level in two.
 */
#define INIT_CALL_AT(level, func) MAKE_INIT_CALL(func, level, at)
#define INIT_CALL_PRE(level, func) MAKE_INIT_CALL(func, level, pre)
#define INIT_CALL_POST(level, func) MAKE_INIT_CALL(func, level, post)

#endif

enum init_level : u32 {
    #define INIT_LEVEL(x) INIT_LEVEL_##x,
    INIT_LEVELS
    #undef INIT_LEVEL

    NUM_INIT_LEVELS,
};
enum init_level init_level(void);
bool init_level_at_least(enum init_level);
bool init_level_below(enum init_level);

void init_level_raise(enum init_level next_level);
