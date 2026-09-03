#pragma once

#include <common/attributes.h>
#include <common/types.h>
#include <common/error.h>

#include <private/init_level.h>

#include <bug.h>

typedef error_t (*init_call_t)(void);

enum init_level : u32 {
    #define INIT_LEVEL(x) INIT_LEVEL_##x,
    INIT_LEVELS
    #undef INIT_LEVEL

    NUM_INIT_LEVELS,
};

#define VALIDATE_INIT_LEVEL(level)                         \
    STATIC_ASSERT(                                         \
        INIT_LEVEL_##level >= 0,                           \
        "Please use one of init_level enumeration values " \
        "(without a prefix)"                               \
    )

#define MAKE_INIT_CALL(func, level, type)                  \
    VALIDATE_INIT_LEVEL(level);                            \
    static const init_call_t init_call_hook_##func = func

#define INIT_CALL_AT(level, func) MAKE_INIT_CALL(func, level, at)
#define INIT_CALL_PRE(level, func) MAKE_INIT_CALL(func, level, pre)
#define INIT_CALL_POST(level, func) MAKE_INIT_CALL(func, level, post)

// Tests run with everything considered initialized unless lowered
UNUSED_DECL static enum init_level s_test_init_level = NUM_INIT_LEVELS;

static inline bool init_level_at_least(enum init_level level)
{
    return s_test_init_level >= level;
}

static inline bool init_level_below(enum init_level level)
{
    return s_test_init_level < level;
}

#define MAKE_INIT_LEVEL_BUG_ON(predicate, level)    \
    do {                                           \
        VALIDATE_INIT_LEVEL(level);                \
        BUG_ON(predicate(INIT_LEVEL_##level));     \
    } while (0)

#define BUG_ON_INIT_LEVEL_BELOW(level) \
    MAKE_INIT_LEVEL_BUG_ON(init_level_below, level)
#define BUG_ON_INIT_LEVEL_AT_OR_ABOVE(level) \
    MAKE_INIT_LEVEL_BUG_ON(init_level_at_least, level)
